// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_SUPERBLOCK_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_SUPERBLOCK_H_

#include <disk_inspector/common_types.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

#include "allocator/inode_manager.h"
#include "minfs_private.h"

namespace minfs {

// Total number of fields in the on-disk superblock structure.
constexpr uint32_t kSuperblockNumElements = 28;
constexpr char kSuperBlockName[] = "superblock";
constexpr char kBackupSuperBlockName[] = "backup superblock";

enum class SuperblockType {
  kPrimary,
  kBackup,
};

class SuperBlockObject : public disk_inspector::DiskObject {
 public:
  SuperBlockObject() = delete;
  SuperBlockObject(const SuperBlockObject&) = delete;
  SuperBlockObject(SuperBlockObject&&) = delete;
  SuperBlockObject& operator=(const SuperBlockObject&) = delete;
  SuperBlockObject& operator=(SuperBlockObject&&) = delete;

  SuperBlockObject(const Superblock& sb, SuperblockType block_type)
      : sb_(sb), block_type_(block_type) {}

  // DiskObject interface:
  const char* GetName() const override {
    return (block_type_ == SuperblockType::kBackup) ? kBackupSuperBlockName : kSuperBlockName;
  }

  uint32_t GetNumElements() const override { return kSuperblockNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // minfs superblock object.
  const Superblock sb_;
  // whether this object is the backup superblock;
  const SuperblockType block_type_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_SUPERBLOCK_H_
