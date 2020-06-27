// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_DIRECTORY_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_DIRECTORY_H_

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fs/trace.h>
#include <fs/vnode.h>
#include <minfs/format.h>
#include <minfs/minfs.h>
#include <minfs/superblock.h>
#include <minfs/transaction_limits.h>
#include <minfs/writeback.h>

#include "vnode.h"

namespace minfs {

struct DirectoryOffset {
  size_t off = 0;       // Offset in directory of current record
  size_t off_prev = 0;  // Offset in directory of previous record
};

struct DirArgs {
  fbl::StringPiece name;
  ino_t ino = 0;
  uint32_t type = 0;
  uint32_t reclen = 0;
  Transaction* transaction = nullptr;
  DirectoryOffset offs;
};

// A specialization of the Minfs Vnode which implements a directory interface.
class Directory final : public VnodeMinfs, public fbl::Recyclable<Directory> {
 public:
  Directory(Minfs* fs);
  ~Directory() final;

  // fbl::Recyclable interface.
  void fbl_recycle() final { VnodeMinfs::fbl_recycle(); }

  // fs::Vnode interface.
  fs::VnodeProtocolSet GetProtocols() const final;
  zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                      size_t* out_actual) final;
  zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) final;
  zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, fbl::StringPiece oldname,
                     fbl::StringPiece newname, bool src_must_be_dir, bool dst_must_be_dir) final;
  zx_status_t Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> target) final;
  zx_status_t Truncate(size_t len) final;

 private:
  // minfs::Vnode interface.
  zx_status_t CanUnlink() const final;
  blk_t GetBlockCount() const final;
  uint64_t GetSize() const final;
  void SetSize(uint32_t new_size) final;
  void AcquireWritableBlock(Transaction* transaction, blk_t local_bno, blk_t old_bno,
                            blk_t* out_bno) final;
  void DeleteBlock(PendingWork* transaction, blk_t local_bno, blk_t old_bno, bool indirect) final;
  bool IsDirectory() const final { return true; }

#ifdef __Fuchsia__
  void IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                      blk_t count) final;
  bool HasPendingAllocation(blk_t vmo_offset) final;
  void CancelPendingWriteback() final;
#endif

  // Other, non-virtual methods:

  // Lookup which can traverse '..'
  zx_status_t LookupInternal(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name);

  // Verify that the 'newdir' inode is not a subdirectory of this Vnode.
  // Traces the path from newdir back to the root inode.
  zx_status_t CheckNotSubdirectory(fbl::RefPtr<Directory> newdir);

  using DirentCallback = zx_status_t (*)(fbl::RefPtr<Directory>, Dirent*, DirArgs*);

  // Enumerates directories.
  zx_status_t ForEachDirent(DirArgs* args, const DirentCallback func);

  // Directory callback functions.
  //
  // The following functions are passable to |ForEachDirent|, which reads the parent directory,
  // one dirent at a time, and passes each entry to the callback function, along with the DirArgs
  // information passed to the initial call of |ForEachDirent|.
  static zx_status_t DirentCallbackFind(fbl::RefPtr<Directory>, Dirent*, DirArgs*);
  static zx_status_t DirentCallbackUnlink(fbl::RefPtr<Directory>, Dirent*, DirArgs*);
  static zx_status_t DirentCallbackForceUnlink(fbl::RefPtr<Directory>, Dirent*, DirArgs*);
  static zx_status_t DirentCallbackAttemptRename(fbl::RefPtr<Directory>, Dirent*, DirArgs*);
  static zx_status_t DirentCallbackUpdateInode(fbl::RefPtr<Directory>, Dirent*, DirArgs*);
  static zx_status_t DirentCallbackFindSpace(fbl::RefPtr<Directory>, Dirent*, DirArgs*);

  // Appends a new directory at the specified offset within |args|. This requires a prior call to
  // DirentCallbackFindSpace to find an offset where there is space for the direntry. It takes
  // the same |args| that were passed into DirentCallbackFindSpace.
  zx_status_t AppendDirent(DirArgs* args);

  zx_status_t UnlinkChild(Transaction* transaction, fbl::RefPtr<VnodeMinfs> child, Dirent* de,
                          DirectoryOffset* offs);
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_DIRECTORY_H_
