// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <utility>

#include <minfs/allocator_reservation.h>

#include "allocator.h"

namespace minfs {

AllocatorReservation::AllocatorReservation(Allocator* allocator) : allocator_(*allocator) {}
AllocatorReservation::~AllocatorReservation() { Cancel(); }

zx_status_t AllocatorReservation::Reserve(PendingWork* transaction, size_t reserved) {
  if (reserved_ != 0) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = allocator_.Reserve({}, transaction, reserved);
  if (status == ZX_OK) {
    reserved_ = reserved;
  }
  return status;
}

size_t AllocatorReservation::Allocate() {
  ZX_ASSERT(reserved_ > 0);
  reserved_--;
  return allocator_.Allocate({}, this);
}

void AllocatorReservation::Deallocate(size_t element) {
  allocator_.Free(this, element);
}

#ifdef __Fuchsia__
size_t AllocatorReservation::Swap(size_t old_index) {
  if (old_index > 0) {
    allocator_.Free(this, old_index);
  }
  return Allocate();
}

#endif

void AllocatorReservation::Cancel() {
  if (reserved_ > 0) {
    allocator_.Unreserve({}, reserved_);
    reserved_ = 0;
  }
}

PendingAllocations& AllocatorReservation::GetPendingAllocations(Allocator* allocator) {
  if (!allocations_) {
    allocations_ = std::make_unique<PendingAllocations>(allocator);
  }
  return *allocations_;
}

PendingDeallocations& AllocatorReservation::GetPendingDeallocations(Allocator* allocator) {
  if (!deallocations_) {
    deallocations_ = std::make_unique<PendingDeallocations>(allocator);
  }
  return *deallocations_;
}

void AllocatorReservation::Commit(PendingWork* transaction) {
  allocator_.Commit(transaction, this);
}

}  // namespace minfs
