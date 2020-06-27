// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage.h"

#include <utility>

#include "allocator.h"

namespace minfs {

PersistentStorage::PersistentStorage(block_client::BlockDevice* device, SuperblockManager* sb,
                                     size_t unit_size, GrowHandler grow_cb,
                                     AllocatorMetadata metadata)
    : device_(device),
      unit_size_(unit_size),
      sb_(sb),
      grow_cb_(std::move(grow_cb)),
      metadata_(std::move(metadata)) {}

zx_status_t PersistentStorage::AttachVmo(const zx::vmo& vmo, storage::OwnedVmoid* out) {
  return device_->BlockAttachVmo(vmo, &out->GetReference(device_));
}

zx_status_t PersistentStorage::Extend(PendingWork* write_transaction, WriteData data,
                                      GrowMapCallback grow_map) {
  TRACE_DURATION("minfs", "Minfs::PersistentStorage::Extend");
  ZX_DEBUG_ASSERT(write_transaction != nullptr);
  if (!metadata_.UsingFvm()) {
    return ZX_ERR_NO_SPACE;
  }
  uint32_t data_slices_diff = 1;

  // Determine if we will have enough space in the bitmap slice
  // to grow |data_slices_diff| data slices.

  // How large is the bitmap right now?
  uint32_t bitmap_slices = metadata_.Fvm().MetadataSlices();
  uint32_t bitmap_blocks = metadata_.Fvm().UnitsPerSlices(bitmap_slices, kMinfsBlockSize);

  // How large does the bitmap need to be?
  uint32_t data_slices = metadata_.Fvm().DataSlices();
  uint32_t data_slices_new = data_slices + data_slices_diff;

  uint32_t pool_size =
      metadata_.Fvm().UnitsPerSlices(data_slices_new, static_cast<uint32_t>(unit_size_));
  uint32_t bitmap_blocks_new = BitmapBlocksForSize(pool_size);

  if (bitmap_blocks_new > bitmap_blocks) {
    // TODO(smklein): Grow the bitmap another slice.
    // TODO(planders): Once we start growing the [block] bitmap,
    //                 we will need to start growing the journal as well.
    FS_TRACE_ERROR("Minfs allocator needs to increase bitmap size\n");
    return ZX_ERR_NO_SPACE;
  }

  // Make the request to the FVM.
  extend_request_t request;
  request.length = data_slices_diff;
  request.offset = metadata_.Fvm().BlocksToSlices(metadata_.DataStartBlock()) + data_slices;

  zx_status_t status = device_->VolumeExtend(request.offset, request.length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs::PersistentStorage::Extend failed to grow (on disk): %d\n", status);
    return status;
  }

  if (grow_cb_) {
    if ((status = grow_cb_(pool_size)) != ZX_OK) {
      FS_TRACE_ERROR("minfs::Allocator grow callback failure: %d\n", status);
      return status;
    }
  }

  // Extend the in memory representation of our allocation pool -- it grew!
  size_t old_pool_size;
  if ((status = grow_map(pool_size, &old_pool_size)) != ZX_OK) {
    return status;
  }

  metadata_.Fvm().SetDataSlices(data_slices_new);
  metadata_.SetPoolTotal(pool_size);
  sb_->Write(write_transaction, UpdateBackupSuperblock::kUpdate);

  // Update the block bitmap.
  PersistRange(write_transaction, data, old_pool_size, pool_size - old_pool_size);
  return ZX_OK;
}

}  // namespace minfs
