// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_VNODE_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_VNODE_H_

#include <inttypes.h>

#include <memory>
#include <utility>

#ifdef __Fuchsia__
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/minfs/llcpp/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <fbl/auto_lock.h>
#include <fs/remote.h>
#include <fs/watcher.h>

#include "vnode_allocation.h"
#endif

#include <lib/zircon-internal/fnv1hash.h>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fs/locking.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <minfs/format.h>
#include <minfs/minfs.h>
#include <minfs/transaction_limits.h>
#include <minfs/writeback.h>

#include "lazy_buffer.h"
#include "vnode_mapper.h"

namespace minfs {

// Used by fsck
class MinfsChecker;
class Minfs;

// An abstract Vnode class contains the following:
//
// - A VMO, holding the in-memory representation of data stored persistently.
// - An inode, holding the root of this node's metadata.
//
// This class is capable of writing, reading, and truncating the node's data
// in a linear block-address space.
#ifdef __Fuchsia__
class VnodeMinfs : public fs::Vnode,
                   public fbl::SinglyLinkedListable<VnodeMinfs*>,
                   public fbl::Recyclable<VnodeMinfs>,
                   llcpp::fuchsia::minfs::Minfs::Interface {
#else
class VnodeMinfs : public fs::Vnode,
                   public fbl::SinglyLinkedListable<VnodeMinfs*>,
                   public fbl::Recyclable<VnodeMinfs> {
#endif
 public:
  virtual ~VnodeMinfs();

  // Allocates a new Vnode and initializes the in-memory inode structure given the type, where
  // type is one of:
  // - kMinfsTypeFile
  // - kMinfsTypeDir
  //
  // Sets create / modify times of the new node.
  // Does not allocate an inode number for the Vnode.
  static void Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out);

  // Allocates a Vnode, loading |ino| from storage.
  //
  // Doesn't update create / modify times of the node.
  static void Recreate(Minfs* fs, ino_t ino, fbl::RefPtr<VnodeMinfs>* out);

  bool IsUnlinked() const { return inode_.link_count == 0; }

  const Inode* GetInode() const { return &inode_; }
  Inode* GetMutableInode() { return &inode_; }
  ino_t GetIno() const { return ino_; }

  ino_t GetKey() const { return ino_; }
  // Should only be called once for the VnodeMinfs lifecycle.
  void SetIno(ino_t ino);

  void SetNextInode(ino_t ino) { inode_.next_inode = ino; }
  void SetLastInode(ino_t ino) { inode_.last_inode = ino; }

  void AddLink();

  void MarkPurged() { inode_.magic = kMinfsMagicPurged; }

  static size_t GetHash(ino_t key) { return fnv1a_tiny(key, kMinfsHashBits); }

  // fs::Vnode interface (invoked publicly).
#ifdef __Fuchsia__
  void HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn) final;
#endif
  using fs::Vnode::Open;
  zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) final;
  zx_status_t Close() final;

  // fbl::Recyclable interface.
  void fbl_recycle() override;

  // Queries the underlying vnode to ask if it may be unlinked.
  //
  // If the response is not ZX_OK, operations to unlink (or rename on top of) this
  // vnode will fail.
  virtual zx_status_t CanUnlink() const = 0;

  // Returns the current block count of the vnode.
  virtual blk_t GetBlockCount() const = 0;

  // Returns the total size of the vnode.
  virtual uint64_t GetSize() const = 0;

  // Returns if the node is a directory.
  // TODO(fxb/39864): This function is used only within minfs to implement unlinking and renaming.
  // Consider replacing this with the more general |Vnode::GetProtocols|.
  virtual bool IsDirectory() const = 0;

  // Sets the new size of the vnode.
  // Should update the in-memory representation of the Vnode, but not necessarily
  // write it out to persistent storage.
  //
  // TODO: Upgrade internal size to 64-bit integer.
  virtual void SetSize(uint32_t new_size) = 0;

  // Accesses a block in the vnode at |vmo_offset| relative to the start of the file,
  // which was previously at the device offset |dev_offset|.
  //
  // If the block was not previously allocated, |dev_offset| is zero.
  // |*out_dev_offset| must contain the new value of the device offset to use when writing
  // to this part of the Vnode. By default, it is set to |dev_offset|.
  //
  // |*out_dev_offset| may be passed to |IssueWriteback| as |dev_offset|.
  virtual void AcquireWritableBlock(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                                    blk_t* out_dev_offset) = 0;

  // Deletes the block at |vmo_offset| within the file, corresponding to on-disk block |dev_offset|
  // (zero if unallocated). |indirect| specifies whether the block is a direct or indirect block.
  virtual void DeleteBlock(PendingWork* transaction, blk_t vmo_offset, blk_t dev_offset,
                           bool indirect) = 0;

#ifdef __Fuchsia__
  // Instructs the Vnode to write out |count| blocks of the vnode, starting at local
  // offset |vmo_offset|, corresponding to on-disk offset |dev_offset|.
  virtual void IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                              blk_t count) = 0;

  // Queries the node, returning |true| if the node has an in-flight operation on |vmo_offset|
  // that has not yet been enqueued to the writeback pipeline.
  virtual bool HasPendingAllocation(blk_t vmo_offset) = 0;

  // Instructs the node to cancel all pending writeback operations that have not yet been
  // enqueued to the writeback pipeline.
  //
  // This method is used exclusively when deleting nodes.
  virtual void CancelPendingWriteback() = 0;

  // Minfs FIDL interface.
  void GetMetrics(GetMetricsCompleter::Sync completer) final;
  void ToggleMetrics(bool enabled, ToggleMetricsCompleter::Sync completer) final;
  void GetAllocatedRegions(GetAllocatedRegionsCompleter::Sync completer) final;
  void GetMountState(GetMountStateCompleter::Sync completer) final;

#endif
  Minfs* Vfs() { return fs_; }

  // Local implementations of read, write, and truncate functions which
  // may operate on either files or directories.
  zx_status_t ReadInternal(PendingWork* transaction, void* data, size_t len, size_t off,
                           size_t* actual);
  zx_status_t ReadExactInternal(PendingWork* transaction, void* data, size_t len, size_t off);
  zx_status_t WriteInternal(Transaction* transaction, const void* data, size_t len, size_t off,
                            size_t* actual);
  zx_status_t WriteExactInternal(Transaction* transaction, const void* data, size_t len,
                                 size_t off);
  zx_status_t TruncateInternal(Transaction* transaction, size_t len);

  // Update the vnode's inode and write it to disk.
  void InodeSync(PendingWork* transaction, uint32_t flags);

  // Decrements the inode link count to a vnode.
  // Writes the inode back to |transaction|.
  //
  // If the link count becomes zero, the node either:
  // 1) Calls |Purge()| (if no open fds exist), or
  // 2) Adds itself to the "unlinked list", to be purged later.
  [[nodiscard]] zx_status_t RemoveInodeLink(Transaction* transaction);

  // Allocates an indirect block.
  void AllocateIndirect(PendingWork* transaction, blk_t* block);

  // Initializes (if necessary) and returns the indirect file.
  [[nodiscard]] zx::status<LazyBuffer*> GetIndirectFile();

  // Deletes all blocks (relative to a file) from "start" (inclusive) to the end
  // of the file. Does not update mtime/atime.
  // This can be extended to return indices of deleted bnos, or to delete a specific number of
  // bnos
  zx_status_t BlocksShrink(PendingWork* transaction, blk_t start);

  // TODO(smklein): These operations and members are protected as a historical artifact
  // of "File + Directory + Vnode" being a single class. They should be transitioned to
  // private.
 protected:
  VnodeMinfs(Minfs* fs);

  // fs::Vnode interface.
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t SetAttributes(fs::VnodeAttributesUpdate a) final;
#ifdef __Fuchsia__
  zx_status_t QueryFilesystem(llcpp::fuchsia::io::FilesystemInfo* out) final;
  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) final;
#endif

  // Although file sizes don't need to be block-aligned, the underlying VMO is
  // always kept at a size which is a multiple of |kMinfsBlockSize|.
  //
  // When a Vnode is truncated to a size larger than |inode_.size|, it is
  // assumed that any space between |inode_.size| and the nearest block is
  // filled with zeroes in the internal VMO. This function validates that
  // assumption.
  void ValidateVmoTail(uint64_t inode_size) const;

  // Get the disk block 'bno' corresponding to the 'n' block
  //
  // May or may not allocate |bno|; certain Vnodes (like File) delay allocation
  // until writeback, and will return a sentinel value of zero.
  //
  // TODO: Use types to represent that |bno|, as an output, is optional.
  zx_status_t BlockGetWritable(Transaction* transaction, blk_t n, blk_t* bno);

  // Get the disk block 'bno' corresponding to relative block address |n| within the file.
  // Does not allocate any blocks, direct or indirect, to acquire this block.
  zx_status_t BlockGetReadable(blk_t n, blk_t* bno);

  // Deletes this Vnode from disk, freeing the inode and blocks.
  //
  // Must only be called on Vnodes which
  // - Have no open fds
  // - Are fully unlinked (link count == 0)
  void Purge(Transaction* transaction);

#ifdef __Fuchsia__
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;

  void Sync(SyncCallback closure) final;
  zx_status_t AttachRemote(fs::MountChannel h) final;
  zx_status_t InitVmo(PendingWork* transaction);

  // Use the watcher container to implement a directory watcher
  void Notify(fbl::StringPiece name, unsigned event) final;
  zx_status_t WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;
#endif
  uint32_t FdCount() const { return fd_count_; }

  Minfs* const fs_;
#ifdef __Fuchsia__
  // TODO(smklein): When we have can register MinFS as a pager service, and
  // it can properly handle pages faults on a vnode's contents, then we can
  // avoid reading the entire file up-front. Until then, read the contents of
  // a VMO into memory when it is read/written.
  zx::vmo vmo_{};
  uint64_t vmo_size_ = 0;
  storage::Vmoid vmoid_;
  fs::WatcherContainer watcher_{};
#endif

  // vnode_mapper.cc explains what this is and the code there is responsible for manipulating it. It
  // is created on-demand.
  std::unique_ptr<LazyBuffer> indirect_file_;

  ino_t ino_{};

  // DataBlockAssigner may modify this field asynchronously, so a valid Transaction object must
  // be held before accessing it.
  Inode inode_{};

  // This field tracks the current number of file descriptors with
  // an open reference to this Vnode. Notably, this is distinct from the
  // VnodeMinfs's own refcount, since there may still be filesystem
  // work to do after the last file descriptor has been closed.
  uint32_t fd_count_{};
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_VNODE_H_
