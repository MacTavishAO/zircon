// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>
#include <err.h>
#include <string.h>
#include <zircon/types.h>

#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>
#include <arch/x86/mp.h>
#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

void x86_reset_tss_io_bitmap(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  tss_t* tss = &x86_get_percpu()->default_tss;
  auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);

  bitmap_set(tss_bitmap, 0, IO_BITMAP_BITS);
}

static void x86_clear_tss_io_bitmap(const bitmap::RleBitmap& bitmap) {
  DEBUG_ASSERT(arch_ints_disabled());
  tss_t* tss = &x86_get_percpu()->default_tss;

  auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);
  for (const auto& extent : bitmap) {
    DEBUG_ASSERT(extent.bitoff + extent.bitlen <= IO_BITMAP_BITS);
    bitmap_set(tss_bitmap, static_cast<int>(extent.bitoff), static_cast<int>(extent.bitlen));
  }
}

void x86_clear_tss_io_bitmap(IoBitmap& io_bitmap) {
  AutoSpinLockNoIrqSave guard(&io_bitmap.lock_);
  if (!io_bitmap.bitmap_)
    return;

  x86_clear_tss_io_bitmap(*io_bitmap.bitmap_);
}

static void x86_set_tss_io_bitmap(const bitmap::RleBitmap& bitmap) {
  DEBUG_ASSERT(arch_ints_disabled());
  tss_t* tss = &x86_get_percpu()->default_tss;

  auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);
  for (const auto& extent : bitmap) {
    DEBUG_ASSERT(extent.bitoff + extent.bitlen <= IO_BITMAP_BITS);
    bitmap_clear(tss_bitmap, static_cast<int>(extent.bitoff), static_cast<int>(extent.bitlen));
  }
}

void x86_set_tss_io_bitmap(IoBitmap& io_bitmap) {
  AutoSpinLockNoIrqSave guard(&io_bitmap.lock_);
  if (!io_bitmap.bitmap_)
    return;

  x86_set_tss_io_bitmap(*io_bitmap.bitmap_);
}

IoBitmap* IoBitmap::GetCurrent() {
  // Fetch current thread's address space. If we have no address space (e.g.,
  // the idle thread), we also don't have an IO Bitmap.
  struct vmm_aspace* aspace = Thread::Current::Get()->aspace_;
  if (aspace == nullptr) {
    return nullptr;
  }

  return &vmm_aspace_to_obj(aspace)->arch_aspace().io_bitmap();
}

IoBitmap::~IoBitmap() {}

struct ioport_update_context {
  // IoBitmap that we're trying to update
  IoBitmap* io_bitmap;
};

void IoBitmap::UpdateTask(void* raw_context) {
  DEBUG_ASSERT(arch_ints_disabled());
  struct ioport_update_context* context = (struct ioport_update_context*)raw_context;
  DEBUG_ASSERT(context->io_bitmap != nullptr);

  // If our CPU's active bitmap matches the one that has been updated,
  // reprogram the hardware to match.
  IoBitmap* io_bitmap = GetCurrent();
  if (io_bitmap == context->io_bitmap) {
    AutoSpinLockNoIrqSave guard(&io_bitmap->lock_);
    // This is overkill, but it's much simpler to reason about
    x86_reset_tss_io_bitmap();
    x86_set_tss_io_bitmap(*io_bitmap->bitmap_);
  }
}

int IoBitmap::SetIoBitmap(uint32_t port, uint32_t len, bool enable) {
  DEBUG_ASSERT(!arch_ints_disabled());

  if ((port + len < port) || (port + len > IO_BITMAP_BITS))
    return ZX_ERR_INVALID_ARGS;

  ktl::unique_ptr<bitmap::RleBitmap> optimistic_bitmap;
  if (!bitmap_) {
    // Optimistically allocate a bitmap structure if we don't have one, and
    // we'll see if we actually need this allocation later.  In the common
    // case, when we make the allocation we will use it.
    fbl::AllocChecker ac;
    optimistic_bitmap.reset(new (&ac) bitmap::RleBitmap());
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Create a free-list in case any of our bitmap operations need to free any
  // nodes.
  bitmap::RleBitmap::FreeList bitmap_freelist;

  // Optimistically allocate an element for the bitmap, in case we need one.
  {
    fbl::AllocChecker ac;
    bitmap_freelist.push_back(
        ktl::unique_ptr<bitmap::RleBitmapElement>(new (&ac) bitmap::RleBitmapElement()));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  zx_status_t status = ZX_OK;

  {
    InterruptDisableGuard intd;

    do {
      AutoSpinLockNoIrqSave guard(&lock_);

      if (!bitmap_) {
        bitmap_ = ktl::move(optimistic_bitmap);
      }
      DEBUG_ASSERT(bitmap_);

      status = enable ? bitmap_->SetNoAlloc(port, port + len, &bitmap_freelist)
                      : bitmap_->ClearNoAlloc(port, port + len, &bitmap_freelist);
      if (status != ZX_OK) {
        break;
      }

      IoBitmap* current = GetCurrent();
      if (this == current) {
        // Set the io bitmap in the tss (the tss IO bitmap has reversed polarity)
        tss_t* tss = &x86_get_percpu()->default_tss;
        if (enable) {
          bitmap_clear(reinterpret_cast<unsigned long*>(tss->tss_bitmap), port, len);
        } else {
          bitmap_set(reinterpret_cast<unsigned long*>(tss->tss_bitmap), port, len);
        }
      }
    } while (0);

    // Let all other CPUs know about the update
    if (status == ZX_OK) {
      struct ioport_update_context task_context = {.io_bitmap = this};
      mp_sync_exec(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, IoBitmap::UpdateTask, &task_context);
    }
  }

  return status;
}
