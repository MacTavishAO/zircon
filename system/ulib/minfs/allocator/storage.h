// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file represents the interface used by the allocator to interact with
// the underlying storage medium.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_STORAGE_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_STORAGE_H_

#include <fbl/function.h>
#include <fbl/macros.h>
#include <fs/transaction/buffered_operations_builder.h>
#include <minfs/superblock.h>
#include <storage/operation/operation.h>

#ifdef __Fuchsia__
#include <block-client/cpp/block-device.h>
#include <storage/buffer/owned_vmoid.h>
#endif

#include "metadata.h"

namespace minfs {

using GrowMapCallback = fbl::Function<zx_status_t(size_t pool_size, size_t* old_pool_size)>;

// Interface for an Allocator's underlying storage.
class AllocatorStorage {
 public:
  AllocatorStorage() = default;
  AllocatorStorage(const AllocatorStorage&) = delete;
  AllocatorStorage& operator=(const AllocatorStorage&) = delete;
  virtual ~AllocatorStorage() {}

#ifdef __Fuchsia__
  virtual zx_status_t AttachVmo(const zx::vmo& vmo, storage::OwnedVmoid* vmoid) = 0;
#endif

  // Loads data from disk into |data| using |builder|.
  // The implementation of this class is expected to use the builder to complete
  // the  request, which means that it should provide the type of data expected
  // by the builder. Specifically, all that should be needed from |data| on host
  // code is access to a raw pointer, and all that should be needed on Fuchsia
  // code is the vmoid that identifies the buffer.
  // For more details consult the BufferedOperationsBuilder documentation.
  virtual void Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) = 0;

  // Extend the on-disk extent containing map_.
  virtual zx_status_t Extend(PendingWork* transaction, WriteData data,
                             GrowMapCallback grow_map) = 0;

  // Returns the number of unallocated elements.
  virtual uint32_t PoolAvailable() const = 0;

  // Returns the total number of elements.
  virtual uint32_t PoolTotal() const = 0;

  // The number of blocks necessary to store |PoolTotal()| elements.
  uint32_t PoolBlocks() const;

  // Persists the map at range |index| - |index + count|.
  virtual void PersistRange(PendingWork* transaction, WriteData data, size_t index,
                            size_t count) = 0;

  // Marks |count| elements allocated and persists the latest data.
  virtual void PersistAllocate(PendingWork* transaction, size_t count) = 0;

  // Marks |count| elements released and persists the latest data.
  virtual void PersistRelease(PendingWork* transaction, size_t count) = 0;
};

// A type of storage which represents a persistent disk.
class PersistentStorage : public AllocatorStorage {
 public:
  // Callback invoked after the data portion of the allocator grows.
  using GrowHandler = fbl::Function<zx_status_t(uint32_t pool_size)>;

  PersistentStorage() = delete;
  PersistentStorage(const PersistentStorage&) = delete;
  PersistentStorage& operator=(const PersistentStorage&) = delete;

#ifdef __Fuchsia__
  // |grow_cb| is an optional callback to increase the size of the allocator.
  PersistentStorage(block_client::BlockDevice* device, SuperblockManager* sb, size_t unit_size,
                    GrowHandler grow_cb, AllocatorMetadata metadata);
#else
  // |grow_cb| is an optional callback to increase the size of the allocator.
  PersistentStorage(SuperblockManager* sb, size_t unit_size, GrowHandler grow_cb,
                    AllocatorMetadata metadata);
#endif
  ~PersistentStorage() {}

#ifdef __Fuchsia__
  zx_status_t AttachVmo(const zx::vmo& vmo, storage::OwnedVmoid* vmoid) override;
#endif

  void Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) final;

  zx_status_t Extend(PendingWork* transaction, WriteData data, GrowMapCallback grow_map) final;

  uint32_t PoolAvailable() const final { return metadata_.PoolAvailable(); }

  uint32_t PoolTotal() const final { return metadata_.PoolTotal(); }

  void PersistRange(PendingWork* transaction, WriteData data, size_t index, size_t count) final;

  void PersistAllocate(PendingWork* transaction, size_t count) final;

  void PersistRelease(PendingWork* transaction, size_t count) final;

 private:
  // Returns the number of blocks necessary to store a pool containing |size| bits.
  static blk_t BitmapBlocksForSize(size_t size);

#ifdef __Fuchsia__
  block_client::BlockDevice* device_;
  size_t unit_size_;
#endif
  SuperblockManager* sb_;
  GrowHandler grow_cb_;
  AllocatorMetadata metadata_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_STORAGE_H_
