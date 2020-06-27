// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_VM_PMM_NODE_H_
#define ZIRCON_KERNEL_VM_PMM_NODE_H_

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <kernel/event.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <vm/pmm.h>
#include <vm/pmm_checker.h>

#include "pmm_arena.h"

// per numa node collection of pmm arenas and worker threads
class PmmNode {
 public:
  PmmNode();
  ~PmmNode();

  DISALLOW_COPY_ASSIGN_AND_MOVE(PmmNode);

  paddr_t PageToPaddr(const vm_page_t* page) TA_NO_THREAD_SAFETY_ANALYSIS;
  vm_page_t* PaddrToPage(paddr_t addr) TA_NO_THREAD_SAFETY_ANALYSIS;

  // main allocator routines
  zx_status_t AllocPage(uint alloc_flags, vm_page_t** page, paddr_t* pa);
  zx_status_t AllocPages(size_t count, uint alloc_flags, list_node* list);
  zx_status_t AllocRange(paddr_t address, size_t count, list_node* list);
  zx_status_t AllocContiguous(size_t count, uint alloc_flags, uint8_t alignment_log2, paddr_t* pa,
                              list_node* list);
  void FreePage(vm_page* page);
  void FreeList(list_node* list);

  // delayed allocator routines
  void AllocPages(uint alloc_flags, page_request_t* req);
  bool ClearRequest(page_request_t* req);
  void SwapRequest(page_request_t* old, page_request_t* new_req);

  zx_status_t InitReclamation(const uint64_t* watermarks, uint8_t watermark_count,
                              uint64_t debounce, void* context,
                              mem_avail_state_updated_callback_t callback);

  int RequestThreadLoop();
  void InitRequestThread();

  uint64_t CountFreePages() const;
  uint64_t CountTotalBytes() const;

  // printf free and overall state of the internal arenas
  // NOTE: both functions skip mutexes and can be called inside timer or crash context
  // though the data they return may be questionable
  void DumpFree() const TA_NO_THREAD_SAFETY_ANALYSIS;
  void Dump(bool is_panic) const TA_NO_THREAD_SAFETY_ANALYSIS;

  void DumpMemAvailState() const;
  uint64_t DebugNumPagesTillMemState(uint8_t mem_state_idx) const;
  uint8_t DebugMaxMemAvailState() const;

  zx_status_t AddArena(const pmm_arena_info_t* info);

  // Returns the number of arenas.
  size_t NumArenas() const;

  // Copies |count| pmm_arena_info_t objects into |buffer| starting with the |i|-th arena ordered by
  // base address.  For example, passing an |i| of 1 would skip the 1st arena.
  //
  // The objects will be sorted in ascending order by arena base address.
  //
  // Returns ZX_ERR_OUT_OF_RANGE if |count| is 0 or |i| and |count| specificy an invalid range.
  //
  // Returns ZX_ERR_BUFFER_TOO_SMALL if the buffer is too small.
  zx_status_t GetArenaInfo(size_t count, uint64_t i, pmm_arena_info_t* buffer, size_t buffer_size);

  // add new pages to the free queue. used when boostrapping a PmmArena
  void AddFreePages(list_node* list);

  PageQueues* GetPageQueues() { return &page_queues_; }

  // Fill all free pages with a pattern and arm the checker.  See |PmmChecker|.
  //
  // This is a no-op if the checker is not enabled.  See |EnableFreePageFilling|
  void FillFreePagesAndArm();

  // Synchronously walk the PMM's free list and validate each page.  This is an incredibly expensive
  // operation and should only be used for debugging purposes.
  void CheckAllFreePages();

#if __has_feature(address_sanitizer)
  // Synchronously walk the PMM's free list and poison each page.
  void PoisonAllFreePages();
#endif

  // Enable the free fill checker with the specified fill size and begin filling freed pages going
  // forward.  See |PmmChecker| for definition of fill size.
  //
  // Note, pages freed piror to calling this method will remain unfilled.  To fill them, call
  // |FillFreePagesAndArm|.
  void EnableFreePageFilling(size_t fill_size);

  // Disarm and disable the free fill checker.
  void DisableChecker();

  // Return a pointer to this object's free fill checker.
  //
  // For test and diagnostic purposes.
  PmmChecker* Checker() { return &checker_; }

 private:
  void FreePageHelperLocked(vm_page* page) TA_REQ(lock_);
  void FreeListLocked(list_node* list) TA_REQ(lock_);

  void ProcessPendingRequests();

  void UpdateMemAvailStateLocked() TA_REQ(lock_);
  void SetMemAvailStateLocked(uint8_t mem_avail_state) TA_REQ(lock_);

  void IncrementFreeCountLocked(uint64_t amount) TA_REQ(lock_) {
    free_count_ += amount;

    if (unlikely(free_count_ >= mem_avail_state_upper_bound_)) {
      UpdateMemAvailStateLocked();
    }
  }
  void DecrementFreeCountLocked(uint64_t amount) TA_REQ(lock_) {
    DEBUG_ASSERT(free_count_ >= amount);
    free_count_ -= amount;

    if (unlikely(free_count_ <= mem_avail_state_lower_bound_)) {
      UpdateMemAvailStateLocked();
    }
  }

  bool InOomStateLocked() TA_REQ(lock_);

  void AllocPageHelperLocked(vm_page_t* page) TA_REQ(lock_);

  void AsanPoisonPage(vm_page_t*, uint8_t) TA_REQ(lock_);

  void AsanUnpoisonPage(vm_page_t*) TA_REQ(lock_);

  fbl::Canary<fbl::magic("PNOD")> canary_;

  mutable DECLARE_MUTEX(PmmNode) lock_;

  uint64_t arena_cumulative_size_ TA_GUARDED(lock_) = 0;
  uint64_t free_count_ TA_GUARDED(lock_) = 0;

  fbl::SizedDoublyLinkedList<PmmArena*> arena_list_ TA_GUARDED(lock_);

  list_node free_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(free_list_);

  // List of pending requests.
  list_node_t request_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(request_list_);
  // Request currently being processed. This is tracked seperately from request_list_
  // because ClearRequest() handles the two cases differently.
  page_request_t* current_request_ TA_GUARDED(lock_) = nullptr;

  Event free_pages_evt_;
  Event request_evt_;

  uint64_t mem_avail_state_watermarks_[MAX_WATERMARK_COUNT] TA_GUARDED(lock_);
  uint8_t mem_avail_state_watermark_count_ TA_GUARDED(lock_);
  uint8_t mem_avail_state_cur_index_ TA_GUARDED(lock_);
  uint64_t mem_avail_state_debounce_ TA_GUARDED(lock_);
  uint64_t mem_avail_state_upper_bound_ TA_GUARDED(lock_);
  uint64_t mem_avail_state_lower_bound_ TA_GUARDED(lock_);
  void* mem_avail_state_context_ TA_GUARDED(lock_);
  mem_avail_state_updated_callback_t mem_avail_state_callback_ TA_GUARDED(lock_);

  Thread* request_thread_ = nullptr;
  ktl::atomic<bool> request_thread_live_ = true;

  PageQueues page_queues_;

  bool free_fill_enabled_ TA_GUARDED(lock_) = false;
  PmmChecker checker_ TA_GUARDED(lock_);
};

// We don't need to hold the arena lock while executing this, since it is
// only accesses values that are set once during system initialization.
inline vm_page_t* PmmNode::PaddrToPage(paddr_t addr) TA_NO_THREAD_SAFETY_ANALYSIS {
  for (auto& a : arena_list_) {
    if (a.address_in_arena(addr)) {
      size_t index = (addr - a.base()) / PAGE_SIZE;
      return a.get_page(index);
    }
  }
  return nullptr;
}

#endif  // ZIRCON_KERNEL_VM_PMM_NODE_H_
