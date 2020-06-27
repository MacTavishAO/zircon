// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <memory>

#include "inode_manager.h"

namespace minfs {

InodeManager::InodeManager(Bcache* bc, blk_t start_block) : start_block_(start_block), bc_(bc) {}

zx_status_t InodeManager::Create(Bcache* bc, SuperblockManager* sb,
                                 fs::BufferedOperationsBuilder* builder, AllocatorMetadata metadata,
                                 blk_t start_block, size_t inodes,
                                 std::unique_ptr<InodeManager>* out) {
  auto mgr = std::unique_ptr<InodeManager>(new InodeManager(bc, start_block));
  InodeManager* mgr_raw = mgr.get();

  auto grow_cb = [mgr_raw](uint32_t pool_size) { return mgr_raw->Grow(pool_size); };

  zx_status_t status;
  std::unique_ptr<PersistentStorage> storage(
      new PersistentStorage(sb, kMinfsInodeSize, std::move(grow_cb), std::move(metadata)));
  if ((status = Allocator::Create(builder, std::move(storage), &mgr->inode_allocator_)) != ZX_OK) {
    return status;
  }

  *out = std::move(mgr);
  return ZX_OK;
}

void InodeManager::Update(PendingWork* transaction, ino_t ino, const Inode* inode) {
  // Obtain the offset of the inode within its containing block
  const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
  const blk_t inoblock_abs = inoblock_rel + start_block_;
  ZX_DEBUG_ASSERT(inoblock_abs < kFVMBlockDataStart);

  // Since host-side tools don't have "mapped vmos", just read / update /
  // write the single absolute inode block.
  uint8_t inodata[kMinfsBlockSize];
  bc_->Readblk(inoblock_abs, inodata);
  memcpy(inodata + off_of_ino, inode, kMinfsInodeSize);
  bc_->Writeblk(inoblock_abs, inodata);
}

const Allocator* InodeManager::GetInodeAllocator() const { return inode_allocator_.get(); }

void InodeManager::Load(ino_t ino, Inode* out) const {
  // obtain the block of the inode table we need
  uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  uint8_t inodata[kMinfsBlockSize];
  bc_->Readblk(start_block_ + (ino / kMinfsInodesPerBlock), inodata);
  const Inode* inode = reinterpret_cast<const Inode*>((uintptr_t)inodata + off_of_ino);
  memcpy(out, inode, kMinfsInodeSize);
}

zx_status_t InodeManager::Grow(size_t inodes) { return ZX_ERR_NO_SPACE; }

}  // namespace minfs
