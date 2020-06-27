// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "pmm_node.h"

#include <align.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/instrumentation/asan.h>
#include <trace.h>

#include <new>

#include <kernel/mp.h>
#include <kernel/thread.h>
#include <pretty/sizes.h>
#include <vm/bootalloc.h>
#include <vm/page_request.h>
#include <vm/physmap.h>
#include <vm/pmm_checker.h>

#include "fbl/algorithm.h"
#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

KCOUNTER(pmm_alloc_async, "vm.pmm.alloc.async")

namespace {

void noop_callback(void* context, uint8_t idx) {}

}  // namespace

// Poison a page |p| with value |value|. Accesses to a poisoned page via the physmap are not
// allowed and may cause faults or kASAN checks.
void PmmNode::AsanPoisonPage(vm_page_t* p, uint8_t value) {
#if __has_feature(address_sanitizer)
  asan_poison_shadow(reinterpret_cast<uintptr_t>(paddr_to_physmap(p->paddr())), PAGE_SIZE, value);
#endif  // __has_feature(address_sanitizer)
}

// Unpoison a page |p|. Accesses to a unpoisoned pages will not cause KASAN check failures.
void PmmNode::AsanUnpoisonPage(vm_page_t* p) {
#if __has_feature(address_sanitizer)
  asan_unpoison_shadow(reinterpret_cast<uintptr_t>(paddr_to_physmap(p->paddr())), PAGE_SIZE);
#endif  // __has_feature(address_sanitizer)
}

PmmNode::PmmNode() {
  // Initialize the reclaimation watermarks such that system never
  // falls into a low memory state.
  uint64_t default_watermark = 0;
  InitReclamation(&default_watermark, 1, 0, nullptr, noop_callback);
}

PmmNode::~PmmNode() {
  if (request_thread_) {
    request_thread_live_ = false;
    request_evt_.Signal();
    free_pages_evt_.Signal();
    int res = 0;
    request_thread_->Join(&res, ZX_TIME_INFINITE);
    DEBUG_ASSERT(res == 0);
  }
}

// We disable thread safety analysis here, since this function is only called
// during early boot before threading exists.
zx_status_t PmmNode::AddArena(const pmm_arena_info_t* info) TA_NO_THREAD_SAFETY_ANALYSIS {
  dprintf(INFO, "PMM: adding arena %p name '%s' base %#" PRIxPTR " size %#zx\n", info, info->name,
          info->base, info->size);

  // Make sure we're in early boot (ints disabled and no active CPUs according
  // to the scheduler).
  DEBUG_ASSERT(mp_get_active_mask() == 0);
  DEBUG_ASSERT(arch_ints_disabled());

  DEBUG_ASSERT(IS_PAGE_ALIGNED(info->base));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(info->size));
  DEBUG_ASSERT(info->size > 0);

  // allocate a c++ arena object
  PmmArena* arena = new (boot_alloc_mem(sizeof(PmmArena))) PmmArena();

  // initialize the object
  auto status = arena->Init(info, this);
  if (status != ZX_OK) {
    // leaks boot allocator memory
    arena->~PmmArena();
    printf("PMM: pmm_add_arena failed to initialize arena\n");
    return status;
  }

  // walk the arena list, inserting in ascending order of arena base address
  for (auto& a : arena_list_) {
    if (a.base() > arena->base()) {
      arena_list_.insert(a, arena);
      goto done_add;
    }
  }

  // walked off the end, add it to the end of the list
  arena_list_.push_back(arena);

done_add:
  arena_cumulative_size_ += info->size;

  return ZX_OK;
}

size_t PmmNode::NumArenas() const {
  Guard<Mutex> guard{&lock_};
  return arena_list_.size();
}

zx_status_t PmmNode::GetArenaInfo(size_t count, uint64_t i, pmm_arena_info_t* buffer,
                                  size_t buffer_size) {
  Guard<Mutex> guard{&lock_};

  if ((count == 0) || (count + i > arena_list_.size()) || (i >= arena_list_.size())) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const size_t size_required = count * sizeof(pmm_arena_info_t);
  if (buffer_size < size_required) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Skip the first |i| elements.
  auto iter = arena_list_.begin();
  for (uint64_t j = 0; j < i; j++) {
    iter++;
  }

  // Copy the next |count| elements.
  for (uint64_t j = 0; j < count; j++) {
    buffer[j] = iter->info();
    iter++;
  }

  return ZX_OK;
}

// called at boot time as arenas are brought online, no locks are acquired
void PmmNode::AddFreePages(list_node* list) TA_NO_THREAD_SAFETY_ANALYSIS {
  LTRACEF("list %p\n", list);

  vm_page *temp, *page;
  list_for_every_entry_safe (list, page, temp, vm_page, queue_node) {
    list_delete(&page->queue_node);
    list_add_tail(&free_list_, &page->queue_node);
    free_count_++;
  }
  ASSERT(free_count_);
  free_pages_evt_.SignalNoResched();

  LTRACEF("free count now %" PRIu64 "\n", free_count_);
}

void PmmNode::FillFreePagesAndArm() {
  Guard<Mutex> guard{&lock_};

  if (!free_fill_enabled_) {
    return;
  }

  vm_page* page;
  list_for_every_entry (&free_list_, page, vm_page, queue_node) { checker_.FillPattern(page); }

  // Now that every page has been filled, we can arm the checker.
  checker_.Arm();
  printf("PMM: pmm checker is armed, fill size is %lu\n", checker_.GetFillSize());
}

void PmmNode::CheckAllFreePages() {
  Guard<Mutex> guard{&lock_};

  if (!checker_.IsArmed()) {
    return;
  }

  vm_page* page;
  list_for_every_entry (&free_list_, page, vm_page, queue_node) { checker_.AssertPattern(page); }
}

#if __has_feature(address_sanitizer)
void PmmNode::PoisonAllFreePages() {
  Guard<Mutex> guard{&lock_};

  vm_page* page;
  list_for_every_entry (&free_list_, page, vm_page, queue_node) {
    AsanPoisonPage(page, kAsanPmmFreeMagic);
  };
}
#endif  // __has_feature(address_sanitizer)

void PmmNode::EnableFreePageFilling(size_t fill_size) {
  Guard<Mutex> guard{&lock_};
  checker_.SetFillSize(fill_size);
  free_fill_enabled_ = true;
}

void PmmNode::DisableChecker() {
  Guard<Mutex> guard{&lock_};
  checker_.Disarm();
  free_fill_enabled_ = false;
}

void PmmNode::AllocPageHelperLocked(vm_page_t* page) {
  LTRACEF("allocating page %p, pa %#" PRIxPTR ", prev state %s\n", page, page->paddr(),
          page_state_to_string(page->state()));

  AsanUnpoisonPage(page);

  DEBUG_ASSERT(page->is_free());

  page->set_state(VM_PAGE_STATE_ALLOC);

  if (unlikely(free_fill_enabled_)) {
    checker_.AssertPattern(page);
  }
}

zx_status_t PmmNode::AllocPage(uint alloc_flags, vm_page_t** page_out, paddr_t* pa_out) {
  Guard<Mutex> guard{&lock_};

  if (unlikely(InOomStateLocked())) {
    if (alloc_flags & PMM_ALLOC_DELAY_OK) {
      // TODO(stevensd): Differentiate 'cannot allocate now' from 'can never allocate'
      return ZX_ERR_NO_MEMORY;
    }
  }

  vm_page* page = list_remove_head_type(&free_list_, vm_page, queue_node);
  if (!page) {
    return ZX_ERR_NO_MEMORY;
  }

  AllocPageHelperLocked(page);

  DecrementFreeCountLocked(1);

  if (pa_out) {
    *pa_out = page->paddr();
  }

  if (page_out) {
    *page_out = page;
  }

  return ZX_OK;
}

zx_status_t PmmNode::AllocPages(size_t count, uint alloc_flags, list_node* list) {
  LTRACEF("count %zu\n", count);

  // list must be initialized prior to calling this
  DEBUG_ASSERT(list);

  if (unlikely(count == 0)) {
    return ZX_OK;
  } else if (count == 1) {
    vm_page* page;
    zx_status_t status = AllocPage(alloc_flags, &page, nullptr);
    if (likely(status == ZX_OK)) {
      list_add_tail(list, &page->queue_node);
    }
    return status;
  }

  Guard<Mutex> guard{&lock_};

  if (unlikely(count > free_count_)) {
    return ZX_ERR_NO_MEMORY;
  }

  DecrementFreeCountLocked(count);

  if (unlikely(InOomStateLocked())) {
    if (alloc_flags & PMM_ALLOC_DELAY_OK) {
      IncrementFreeCountLocked(count);
      // TODO(stevensd): Differentiate 'cannot allocate now' from 'can never allocate'
      return ZX_ERR_NO_MEMORY;
    }
  }

  auto node = &free_list_;
  while (count-- > 0) {
    node = list_next(&free_list_, node);
    AllocPageHelperLocked(containerof(node, vm_page, queue_node));
  }

  list_node tmp_list = LIST_INITIAL_VALUE(tmp_list);
  list_split_after(&free_list_, node, &tmp_list);
  if (list_is_empty(list)) {
    list_move(&free_list_, list);
  } else {
    list_splice_after(&free_list_, list_peek_tail(list));
  }
  list_move(&tmp_list, &free_list_);

  return ZX_OK;
}

zx_status_t PmmNode::AllocRange(paddr_t address, size_t count, list_node* list) {
  LTRACEF("address %#" PRIxPTR ", count %zu\n", address, count);

  // list must be initialized prior to calling this
  DEBUG_ASSERT(list);

  size_t allocated = 0;
  if (count == 0) {
    return ZX_OK;
  }

  address = ROUNDDOWN(address, PAGE_SIZE);

  Guard<Mutex> guard{&lock_};

  // walk through the arenas, looking to see if the physical page belongs to it
  for (auto& a : arena_list_) {
    while (allocated < count && a.address_in_arena(address)) {
      vm_page_t* page = a.FindSpecific(address);
      if (!page) {
        break;
      }

      if (!page->is_free()) {
        break;
      }

      list_delete(&page->queue_node);

      AllocPageHelperLocked(page);

      list_add_tail(list, &page->queue_node);

      allocated++;
      address += PAGE_SIZE;
      DecrementFreeCountLocked(1);
    }

    if (allocated == count) {
      break;
    }
  }

  if (allocated != count) {
    // we were not able to allocate the entire run, free these pages
    FreeListLocked(list);
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

zx_status_t PmmNode::AllocContiguous(const size_t count, uint alloc_flags, uint8_t alignment_log2,
                                     paddr_t* pa, list_node* list) {
  LTRACEF("count %zu, align %u\n", count, alignment_log2);

  if (count == 0) {
    return ZX_OK;
  }
  if (alignment_log2 < PAGE_SIZE_SHIFT) {
    alignment_log2 = PAGE_SIZE_SHIFT;
  }

  // pa and list must be valid pointers
  DEBUG_ASSERT(pa);
  DEBUG_ASSERT(list);

  Guard<Mutex> guard{&lock_};

  for (auto& a : arena_list_) {
    vm_page_t* p = a.FindFreeContiguous(count, alignment_log2);
    if (!p) {
      continue;
    }

    *pa = p->paddr();

    // remove the pages from the run out of the free list
    for (size_t i = 0; i < count; i++, p++) {
      DEBUG_ASSERT_MSG(p->is_free(), "p %p state %u\n", p, p->state());
      DEBUG_ASSERT(list_in_list(&p->queue_node));

      list_delete(&p->queue_node);
      p->set_state(VM_PAGE_STATE_ALLOC);

      DecrementFreeCountLocked(1);
      AsanUnpoisonPage(p);
      checker_.AssertPattern(p);

      list_add_tail(list, &p->queue_node);
    }

    return ZX_OK;
  }

  LTRACEF("couldn't find run\n");
  return ZX_ERR_NOT_FOUND;
}

void PmmNode::FreePageHelperLocked(vm_page* page) {
  LTRACEF("page %p state %u paddr %#" PRIxPTR "\n", page, page->state(), page->paddr());

  DEBUG_ASSERT(page->state() != VM_PAGE_STATE_OBJECT || page->object.pin_count == 0);
  DEBUG_ASSERT(!page->is_free());

  // mark it free
  page->set_state(VM_PAGE_STATE_FREE);

  if (unlikely(free_fill_enabled_)) {
    checker_.FillPattern(page);
  }

  AsanPoisonPage(page, kAsanPmmFreeMagic);
}

void PmmNode::FreePage(vm_page* page) {
  Guard<Mutex> guard{&lock_};

  // pages freed individually shouldn't be in a queue
  DEBUG_ASSERT(!list_in_list(&page->queue_node));

  FreePageHelperLocked(page);

  // add it to the free queue
  list_add_head(&free_list_, &page->queue_node);

  IncrementFreeCountLocked(1);
}

void PmmNode::FreeListLocked(list_node* list) {
  DEBUG_ASSERT(list);

  // process list backwards so the head is as hot as possible
  uint64_t count = 0;
  for (vm_page* page = list_peek_tail_type(list, vm_page, queue_node); page != nullptr;
       page = list_prev_type(list, &page->queue_node, vm_page, queue_node)) {
    FreePageHelperLocked(page);
    count++;
  }

  // splice list at the head of free_list_
  list_splice_after(list, &free_list_);

  IncrementFreeCountLocked(count);
}

void PmmNode::FreeList(list_node* list) {
  Guard<Mutex> guard{&lock_};

  FreeListLocked(list);
}

void PmmNode::AllocPages(uint alloc_flags, page_request_t* req) {
  kcounter_add(pmm_alloc_async, 1);

  Guard<Mutex> guard{&lock_};
  list_add_tail(&request_list_, &req->provider_node);

  request_evt_.SignalNoResched();
}

bool PmmNode::InOomStateLocked() {
  if (mem_avail_state_cur_index_ == 0) {
    return true;
  }

#if RANDOM_DELAYED_ALLOC
  // Randomly try to make 10% of allocations delayed allocations.
  return rand() < (RAND_MAX / 10);
#else
  return false;
#endif
}

bool PmmNode::ClearRequest(page_request_t* req) {
  Guard<Mutex> guard{&lock_};
  bool res;
  if (list_in_list(&req->provider_node)) {
    // Get rid of our reference to the request and let the client know that we
    // don't need the req->cb_ctx anymore.
    list_delete(&req->provider_node);
    res = true;
  } else {
    // We might still need the reference to the request's context, so tell the caller
    // not to delete the context. That will be done when ProcessPendingRequest sees
    // that current_request_ is null.
    DEBUG_ASSERT(current_request_ == req);
    current_request_ = nullptr;
    res = false;
  }

  if (list_is_empty(&request_list_) && current_request_ == nullptr) {
    request_evt_.Unsignal();
  }

  return res;
}

void PmmNode::SwapRequest(page_request_t* old, page_request_t* new_req) {
  DEBUG_ASSERT(old->cb_ctx == new_req->cb_ctx);
  DEBUG_ASSERT(old->drop_ref_cb == new_req->drop_ref_cb);
  DEBUG_ASSERT(old->pages_available_cb == new_req->pages_available_cb);

  Guard<Mutex> guard{&lock_};

  new_req->length = old->length;
  new_req->offset = old->offset;

  if (old == current_request_) {
    current_request_ = new_req;
  } else if (list_in_list(&old->provider_node)) {
    list_replace_node(&old->provider_node, &new_req->provider_node);
  }
}

void PmmNode::ProcessPendingRequests() {
  Guard<Mutex> guard{&lock_};
  page_request* node = nullptr;
  while ((node = list_peek_head_type(&request_list_, page_request, provider_node)) &&
         mem_avail_state_cur_index_) {
    // Create a local copy of the request because the memory might disappear as
    // soon as we release lock_.
    page_request_t req_copy = *node;

    // Move the request from the list to the current request slot.
    list_delete(&node->provider_node);
    current_request_ = node;
    node = nullptr;

    uint64_t actual_supply;
    guard.CallUnlocked([req_copy, &actual_supply]() {
      // Note that this will call back into ::ClearRequest and
      // clear current_request_ if the request is fulfilled.
      req_copy.pages_available_cb(req_copy.cb_ctx, req_copy.offset, req_copy.length,
                                  &actual_supply);
    });

    if (current_request_ != nullptr && actual_supply != req_copy.length) {
      // If we didn't fully supply the pages and the pending node hasn't been
      // cancelled, then we need to put the pending request and come back to it
      // when more pages are available.
      DEBUG_ASSERT(current_request_->offset == req_copy.offset);
      DEBUG_ASSERT(current_request_->length == req_copy.length);

      current_request_->offset += actual_supply;
      current_request_->length -= actual_supply;

      list_add_head(&request_list_, &current_request_->provider_node);
      current_request_ = nullptr;
    } else {
      // If the request was cancelled or we successfully fulfilled the
      // request, the we need to drop our ref to ctx.
      guard.CallUnlocked([req_copy]() { req_copy.drop_ref_cb(req_copy.cb_ctx); });
    }
  }
}

uint64_t PmmNode::CountFreePages() const TA_NO_THREAD_SAFETY_ANALYSIS { return free_count_; }

uint64_t PmmNode::CountTotalBytes() const TA_NO_THREAD_SAFETY_ANALYSIS {
  return arena_cumulative_size_;
}

void PmmNode::DumpFree() const TA_NO_THREAD_SAFETY_ANALYSIS {
  auto megabytes_free = CountFreePages() / 256u;
  printf(" %zu free MBs\n", megabytes_free);
}

void PmmNode::Dump(bool is_panic) const {
  // No lock analysis here, as we want to just go for it in the panic case without the lock.
  auto dump = [this]() TA_NO_THREAD_SAFETY_ANALYSIS {
    printf("pmm node %p: free_count %zu (%zu bytes), total size %zu\n", this, free_count_,
           free_count_ * PAGE_SIZE, arena_cumulative_size_);
    for (auto& a : arena_list_) {
      a.Dump(false, false);
    }
  };

  if (is_panic) {
    dump();
  } else {
    Guard<Mutex> guard{&lock_};
    dump();
  }
}

int PmmNode::RequestThreadLoop() {
  while (request_thread_live_) {
    // There's a race where the request or free pages can disappear before we start
    // processing them, but that just results in ProcessPendingRequests doing a little
    // extra work before we get back to here and wait again.
    request_evt_.Wait(Deadline::infinite());
    free_pages_evt_.Wait(Deadline::infinite());
    ProcessPendingRequests();
  }
  return 0;
}

zx_status_t PmmNode::InitReclamation(const uint64_t* watermarks, uint8_t watermark_count,
                                     uint64_t debounce, void* context,
                                     mem_avail_state_updated_callback_t callback) {
  if (watermark_count > MAX_WATERMARK_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  uint64_t tmp[MAX_WATERMARK_COUNT];
  uint64_t tmp_debounce = fbl::round_up(debounce, static_cast<uint64_t>(PAGE_SIZE)) / PAGE_SIZE;
  for (uint8_t i = 0; i < watermark_count; i++) {
    tmp[i] = watermarks[i] / PAGE_SIZE;
    if (i > 0) {
      if (tmp[i] <= tmp[i - 1]) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (tmp[i] < tmp_debounce) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  mem_avail_state_watermark_count_ = watermark_count;
  mem_avail_state_debounce_ = tmp_debounce;
  mem_avail_state_context_ = context;
  mem_avail_state_callback_ = callback;
  memcpy(mem_avail_state_watermarks_, tmp, sizeof(mem_avail_state_watermarks_));
  static_assert(sizeof(tmp) == sizeof(mem_avail_state_watermarks_));

  UpdateMemAvailStateLocked();

  return ZX_OK;
}

void PmmNode::UpdateMemAvailStateLocked() {
  // Find the smallest watermark which is greater than the number of free pages.
  uint8_t target = mem_avail_state_watermark_count_;
  for (uint8_t i = 0; i < mem_avail_state_watermark_count_; i++) {
    if (mem_avail_state_watermarks_[i] > free_count_) {
      target = i;
      break;
    }
  }
  SetMemAvailStateLocked(target);
}

void PmmNode::SetMemAvailStateLocked(uint8_t mem_avail_state) {
  mem_avail_state_cur_index_ = mem_avail_state;

  if (mem_avail_state_cur_index_ == 0) {
    free_pages_evt_.Unsignal();
  } else {
    free_pages_evt_.SignalNoResched();
  }

  if (mem_avail_state_cur_index_ > 0) {
    // If there is a smaller watermark, then we transition into that state when the
    // number of free pages drops more than |mem_avail_state_debounce_| pages into that state.
    mem_avail_state_lower_bound_ =
        mem_avail_state_watermarks_[mem_avail_state_cur_index_ - 1] - mem_avail_state_debounce_;
  } else {
    // There is no smaller state, so we can't ever transition down.
    mem_avail_state_lower_bound_ = 0;
  }

  if (mem_avail_state_cur_index_ < mem_avail_state_watermark_count_) {
    // If there is a larger watermark, then we transition out of the current state when
    // the number of free pages exceedes the current state's watermark by at least
    // |mem_avail_state_debounce_|.
    mem_avail_state_upper_bound_ =
        mem_avail_state_watermarks_[mem_avail_state_cur_index_] + mem_avail_state_debounce_;
  } else {
    // There is no larger state, so we can't ever transition up.
    mem_avail_state_upper_bound_ = UINT64_MAX / PAGE_SIZE;
  }

  mem_avail_state_callback_(mem_avail_state_context_, mem_avail_state_cur_index_);
}

void PmmNode::DumpMemAvailState() const {
  Guard<Mutex> guard{&lock_};

  char str[32];
  printf("watermarks: [");
  for (unsigned i = 0; i < mem_avail_state_watermark_count_; i++) {
    format_size(str, sizeof(str), mem_avail_state_watermarks_[i] * PAGE_SIZE);
    printf("%s%s", str, i + 1 == mem_avail_state_watermark_count_ ? "]\n" : ", ");
  }
  format_size(str, sizeof(str), mem_avail_state_debounce_ * PAGE_SIZE);
  printf("debounce: %s\n", str);

  format_size(str, sizeof(str), mem_avail_state_lower_bound_ * PAGE_SIZE);
  printf("current state: %u\ncurrent bounds: [%s, ", mem_avail_state_cur_index_, str);
  format_size(str, sizeof(str), mem_avail_state_upper_bound_ * PAGE_SIZE);
  printf("%s]\n", str);
  format_size(str, sizeof(str), free_count_ * PAGE_SIZE);
  printf("free memory: %s\n", str);
}

uint64_t PmmNode::DebugNumPagesTillMemState(uint8_t mem_state_idx) const {
  Guard<Mutex> guard{&lock_};
  if (mem_avail_state_cur_index_ <= mem_state_idx) {
    // Already in mem_state_idx, or in a state with less available memory than mem_state_idx.
    return 0;
  }
  // We need to either get free_pages below mem_avail_state_watermarks_[mem_state_idx] or, if we are
  // in state (mem_state_idx + 1), we also need to clear the debounce amount. For simplicity we just
  // always allocate the debounce amount as well.
  uint64_t trigger = mem_avail_state_watermarks_[mem_state_idx] - mem_avail_state_debounce_;
  return (free_count_ - trigger);
}

uint8_t PmmNode::DebugMaxMemAvailState() const {
  Guard<Mutex> guard{&lock_};
  return mem_avail_state_watermark_count_;
}

static int pmm_node_request_loop(void* arg) {
  return static_cast<PmmNode*>(arg)->RequestThreadLoop();
}

void PmmNode::InitRequestThread() {
  request_thread_ =
      Thread::Create("pmm-node-request-thread", pmm_node_request_loop, this, HIGH_PRIORITY);
  request_thread_->Resume();
}
