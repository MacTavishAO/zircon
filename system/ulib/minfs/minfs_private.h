// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_MINFS_PRIVATE_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_MINFS_PRIVATE_H_

#include <inttypes.h>

#include <memory>
#include <utility>

#ifdef __Fuchsia__
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/minfs/llcpp/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>

#include <fs/journal/journal.h>
#include <fs/managed_vfs.h>
#include <fs/remote.h>
#include <fs/watcher.h>
#include <minfs/metrics.h>
#endif

#include <lib/zircon-internal/fnv1hash.h>

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fs/inspectable.h>
#include <fs/locking.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/transaction/transaction_handler.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <minfs/format.h>
#include <minfs/minfs.h>
#include <minfs/superblock.h>
#include <minfs/transaction_limits.h>
#include <minfs/writeback.h>

#ifdef __Fuchsia__
#include "vnode_allocation.h"
#endif

#include "allocator/allocator.h"
#include "allocator/inode_manager.h"
#include "vnode.h"

constexpr uint32_t kExtentCount = 6;

// A compile-time debug check, which, if enabled, causes
// inline functions to be expanded to error checking code.
// Since this may be expensive, it is typically turned
// off, except for debugging.
// #define MINFS_PARANOID_MODE

namespace minfs {

#ifdef __Fuchsia__
using MountState = llcpp::fuchsia::minfs::MountState;
#endif  // __Fuchsia__

// SyncVnode flags
constexpr uint32_t kMxFsSyncDefault = 0;  // default: no implicit time update
constexpr uint32_t kMxFsSyncMtime = (1 << 0);
constexpr uint32_t kMxFsSyncCtime = (1 << 1);

constexpr uint32_t kMinfsBlockCacheSize = 64;

// Used by fsck
class MinfsChecker;
class VnodeMinfs;

using SyncCallback = fs::Vnode::SyncCallback;

#ifndef __Fuchsia__

// Store start block + length for all extents. These may differ from info block for
// sparse files.
class BlockOffsets {
 public:
  BlockOffsets(const Bcache& bc, const SuperblockManager& sb);

  blk_t IbmStartBlock() const { return ibm_start_block_; }
  blk_t IbmBlockCount() const { return ibm_block_count_; }

  blk_t AbmStartBlock() const { return abm_start_block_; }
  blk_t AbmBlockCount() const { return abm_block_count_; }

  blk_t InoStartBlock() const { return ino_start_block_; }
  blk_t InoBlockCount() const { return ino_block_count_; }

  blk_t IntegrityStartBlock() const { return integrity_start_block_; }
  blk_t IntegrityBlockCount() const { return integrity_block_count_; }

  blk_t JournalStartBlock() const { return integrity_start_block_ + kBackupSuperblockBlocks; }

  blk_t DatStartBlock() const { return dat_start_block_; }
  blk_t DatBlockCount() const { return dat_block_count_; }

 private:
  blk_t ibm_start_block_;
  blk_t ibm_block_count_;

  blk_t abm_start_block_;
  blk_t abm_block_count_;

  blk_t ino_start_block_;
  blk_t ino_block_count_;

  blk_t integrity_start_block_;
  blk_t integrity_block_count_;

  blk_t dat_start_block_;
  blk_t dat_block_count_;
};
#endif

class TransactionalFs {
 public:
  virtual ~TransactionalFs() = default;

#ifdef __Fuchsia__
  virtual fbl::Mutex* GetLock() const = 0;

  virtual void EnqueueCallback(SyncCallback callback) = 0;
#endif

  // Begin a transaction with |reserve_inodes| inodes and |reserve_blocks| blocks reserved.
  virtual zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                                       std::unique_ptr<Transaction>* transaction_out) = 0;

  // Enqueues a metadata transaction by persisting its contents to disk.
  virtual void CommitTransaction(std::unique_ptr<Transaction> transaction) = 0;

  virtual Bcache* GetMutableBcache() = 0;

  virtual Allocator& GetBlockAllocator() = 0;
  virtual Allocator& GetInodeAllocator() = 0;
};

class InspectableMinfs : public fs::Inspectable {
 public:
  virtual ~InspectableMinfs() {}

  // Returns an immutable reference to the superblock.
  virtual const Superblock& Info() const = 0;

  // Gets an immutable reference to the InodeManager.
  virtual const InspectableInodeManager* GetInodeManager() const = 0;

  // Gets an immutable reference to the block_allocator.
  virtual const Allocator& GetBlockAllocator() const = 0;

#ifndef __Fuchsia__
  // Gets an immutable copy of offsets_.
  virtual const BlockOffsets GetBlockOffsets() const = 0;
#endif
};

class Minfs :
#ifdef __Fuchsia__
    public fs::ManagedVfs,
#else
    public fs::Vfs,
#endif
    public fbl::RefCounted<Minfs>,
    public TransactionalFs,
    public InspectableMinfs {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Minfs);

  ~Minfs();

  // Destroys a "minfs" object, but take back ownership of the bcache object.
  static std::unique_ptr<Bcache> Destroy(std::unique_ptr<Minfs> minfs);

  static zx_status_t Create(std::unique_ptr<Bcache> bc, const MountOptions& options,
                            std::unique_ptr<Minfs>* out);

#ifdef __Fuchsia__
  // Initializes the Minfs journal and writeback queue and resolves any pending disk state (e.g.,
  // resolving unlinked nodes and existing journal entries).
  zx_status_t InitializeJournal(fs::JournalSuperblock journal_superblock);

  // Initializes the Minfs writeback queue and resolves any pending disk state (e.g., resolving
  // unlinked nodes and existing journal entries). Does not enable the journal.
  zx_status_t InitializeUnjournalledWriteback();

  // Queries the superblock flags for FVM as well as underlying FVM, if it exists.
  zx_status_t FVMQuery(fuchsia_hardware_block_volume_VolumeInfo* info) const;
#endif

  // instantiate a vnode from an inode
  // the inode must exist in the file system
  zx_status_t VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino);

  // instantiate a vnode with a new inode
  zx_status_t VnodeNew(Transaction* transaction, fbl::RefPtr<VnodeMinfs>* out, uint32_t type);

  // Insert, lookup, and remove vnode from hash map.
  void VnodeInsert(VnodeMinfs* vn) FS_TA_EXCLUDES(hash_lock_);
  fbl::RefPtr<VnodeMinfs> VnodeLookup(uint32_t ino) FS_TA_EXCLUDES(hash_lock_);
  void VnodeRelease(VnodeMinfs* vn) FS_TA_EXCLUDES(hash_lock_);

  // Allocate a new data block.
  void BlockNew(PendingWork* transaction, blk_t* out_bno);

  // Set/Unset the flags.
  void UpdateFlags(PendingWork* transaction, uint32_t flags, bool set);

  // Mark |in_bno| for de-allocation (if it is > 0), and return a new block |*out_bno|.
  // The swap will not be persisted until the transaction is commited.
  void BlockSwap(Transaction* transaction, blk_t in_bno, blk_t* out_bno);

  // Free ino in inode bitmap, release all blocks held by inode.
  zx_status_t InoFree(Transaction* transaction, VnodeMinfs* vn);

  // Mark |vn| to be unlinked.
  void AddUnlinked(PendingWork* transaction, VnodeMinfs* vn);

  // Remove |vn| from the list of unlinked vnodes.
  void RemoveUnlinked(PendingWork* transaction, VnodeMinfs* vn);

  // Free resources of all vnodes marked unlinked.
  zx_status_t PurgeUnlinked();

  // Writes back an inode into the inode table on persistent storage.
  // Does not modify inode bitmap.
  void InodeUpdate(PendingWork* transaction, ino_t ino, const Inode* inode) {
    inodes_->Update(transaction, ino, inode);
  }

  // Reads an inode from the inode table into memory.
  void InodeLoad(ino_t ino, Inode* out) const { inodes_->Load(ino, out); }

  void ValidateBno(blk_t bno) const {
    ZX_DEBUG_ASSERT(bno != 0);
    ZX_DEBUG_ASSERT(bno < Info().block_count);
  }

  zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                               std::unique_ptr<Transaction>* transaction) final
      __WARN_UNUSED_RESULT;

#ifdef __Fuchsia__
  void EnqueueCallback(SyncCallback callback) final;
#endif

  void EnqueueAllocation(std::unique_ptr<PendingWork> transaction);

  // Complete a transaction by enqueueing its WritebackWork to the WritebackQueue.
  void CommitTransaction(std::unique_ptr<Transaction> transaction) final;

  // Runs fsck at the end of a transaction, just after metadata has been written. Used for testing
  // to be sure that all transactions leave the file system in a good state.
  void FsckAtEndOfTransaction(zx_status_t status);

#ifdef __Fuchsia__
  // Returns the capacity of the writeback buffer, in blocks.
  size_t WritebackCapacity() const {
    // Hardcoded to 10 MB; may be replaced by a more device-specific option
    // in the future.
    return 10 * (1 << 20) / kMinfsBlockSize;
  }

  void SetUnmountCallback(fbl::Closure closure) { on_unmount_ = std::move(closure); }
  void Shutdown(fs::Vfs::ShutdownCallback cb) final;

  // Returns a unique identifier for this instance.
  uint64_t GetFsId() const { return fs_id_; }

  // Signals the completion object as soon as...
  // (1) A sync probe has entered and exited the writeback queue, and
  // (2) The block cache has sync'd with the underlying block device.
  void Sync(SyncCallback closure);
#endif

  // The following methods are used to read one block from the specified extent,
  // from relative block |bno|.
  // |data| is an out parameter that must be a block in size, provided by the caller
  // These functions are single-block and synchronous. On Fuchsia, using the batched read
  // functions is preferred.
  zx_status_t ReadDat(blk_t bno, void* data);

  void SetMetrics(bool enable) {
#ifdef __Fuchsia__
    metrics_.SetEnable(enable);
#endif
  }
  fs::Ticker StartTicker() {
#ifdef __Fuchsia__
    return fs::Ticker(metrics_.Enabled());
#endif
    return fs::Ticker(true);
  }

  // Update aggregate information about VMO initialization.
  void UpdateInitMetrics(uint32_t dnum_count, uint32_t inum_count, uint32_t dinum_count,
                         uint64_t user_data_size, const fs::Duration& duration);
  // Update aggregate information about looking up vnodes by name.
  void UpdateLookupMetrics(bool success, const fs::Duration& duration);
  // Update aggregate information about looking up vnodes by inode.
  void UpdateOpenMetrics(bool cache_hit, const fs::Duration& duration);
  // Update aggregate information about inode creation.
  void UpdateCreateMetrics(bool success, const fs::Duration& duration);
  // Update aggregate information about reading from Vnodes.
  void UpdateReadMetrics(uint64_t size, const fs::Duration& duration);
  // Update aggregate information about writing to Vnodes.
  void UpdateWriteMetrics(uint64_t size, const fs::Duration& duration);
  // Update aggregate information about truncating Vnodes.
  void UpdateTruncateMetrics(const fs::Duration& duration);
  // Update aggregate information about unlinking Vnodes.
  void UpdateUnlinkMetrics(bool success, const fs::Duration& duration);
  // Update aggregate information about renaming Vnodes.
  void UpdateRenameMetrics(bool success, const fs::Duration& duration);

#ifdef __Fuchsia__
  // Acquire a copy of the collected metrics.
  zx_status_t GetMetrics(::llcpp::fuchsia::minfs::Metrics* out) const {
    if (metrics_.Enabled()) {
      metrics_.CopyToFidl(out);
      return ZX_OK;
    }
    return ZX_ERR_UNAVAILABLE;
  }

  // Record the location, size, and number of all non-free block regions.
  fbl::Vector<BlockRegion> GetAllocatedRegions() const;

  // Returns the current state of mounted filesystem.
  // "state" is intentionally losely defined to allow
  // adding more information in the near future.
  MountState GetMountState() const { return mount_state_; }
#endif

  // InspectableFilesystem interface.
  const Superblock& Info() const final { return sb_->Info(); }

  const InspectableInodeManager* GetInodeManager() const final { return inodes_.get(); }

  const Allocator& GetBlockAllocator() const final { return *block_allocator_; }

#ifndef __Fuchsia__
  const BlockOffsets GetBlockOffsets() const final { return offsets_; }
#endif

  zx_status_t ReadBlock(blk_t start_block_num, void* data) const final;

  const TransactionLimits& Limits() const { return limits_; }

#ifdef __Fuchsia__
  fbl::Mutex* GetLock() const final { return &txn_lock_; }

  // Terminates all writeback queues, and flushes pending operations to the underlying device.
  //
  // If |!IsReadonly()|, also sets the dirty bit to a "clean" status.
  void StopWriteback();
#endif

  Bcache* GetMutableBcache() final { return bc_.get(); }

  // TODO(rvargas): Make private.
  std::unique_ptr<Bcache> bc_;

  Allocator& GetBlockAllocator() final { return *block_allocator_; }
  Allocator& GetInodeAllocator() final { return inodes_->inode_allocator(); }

  const MountOptions& mount_options() { return mount_options_; }

 private:
  using HashTable = fbl::HashTable<ino_t, VnodeMinfs*>;

#ifdef __Fuchsia__
  Minfs(std::unique_ptr<Bcache> bc, std::unique_ptr<SuperblockManager> sb,
        std::unique_ptr<Allocator> block_allocator, std::unique_ptr<InodeManager> inodes,
        uint64_t fs_id, const MountOptions& mount_options);
#else
  Minfs(std::unique_ptr<Bcache> bc, std::unique_ptr<SuperblockManager> sb,
        std::unique_ptr<Allocator> block_allocator, std::unique_ptr<InodeManager> inodes,
        BlockOffsets offsets, const MountOptions& mount_options);
#endif

  // Internal version of VnodeLookup which may also return unlinked vnodes.
  fbl::RefPtr<VnodeMinfs> VnodeLookupInternal(uint32_t ino) FS_TA_EXCLUDES(hash_lock_);

  // Check if filesystem is readonly.
  bool IsReadonly() FS_TA_EXCLUDES(vfs_lock_);

  // Find a free inode, allocate it in the inode bitmap, and write it back to disk
  void InoNew(Transaction* transaction, const Inode* inode, ino_t* out_ino);

  // Find an unallocated and unreserved block in the block bitmap starting from block |start|
  zx_status_t FindBlock(size_t start, size_t* blkno_out);

  // Creates an unique identifier for this instance. This is to be called only during
  // "construction".
  static zx_status_t CreateFsId(uint64_t* out);

  // Reads blocks from disk. Only to be called during "construction".
  static zx_status_t ReadInitialBlocks(const Superblock& info, std::unique_ptr<Bcache> bc,
                                       std::unique_ptr<SuperblockManager> sb,
                                       const MountOptions& mount_options,
                                       std::unique_ptr<Minfs>* out_minfs);

  // Updates the clean bit and oldest revision in the super block.
  zx_status_t UpdateCleanBitAndOldestRevision(bool is_clean);

#ifndef __Fuchsia__
  zx_status_t ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max, void* data);
#endif

  // Global information about the filesystem.
  // While Allocator is thread-safe, it is recommended that a valid Transaction object be held
  // while any metadata fields are modified until the time they are enqueued for writeback. This
  // is to avoid modifications from other threads potentially jeopardizing the metadata integrity
  // before it is safely persisted to disk.
  std::unique_ptr<SuperblockManager> sb_;
  std::unique_ptr<Allocator> block_allocator_;
  std::unique_ptr<InodeManager> inodes_;

#ifdef __Fuchsia__
  mutable fbl::Mutex txn_lock_;  // Lock required to start a new Transaction.
  fbl::Mutex hash_lock_;         // Lock required to access the vnode_hash_.
#endif
  // Vnodes exist in the hash table as long as one or more reference exists;
  // when the Vnode is deleted, it is immediately removed from the map.
  HashTable vnode_hash_ FS_TA_GUARDED(hash_lock_){};

#ifdef __Fuchsia__
  fbl::Closure on_unmount_{};
  MinfsMetrics metrics_ = {};
  std::unique_ptr<fs::Journal> journal_;
  uint64_t fs_id_ = 0;
  // TODO(fxb/51057): Git rid of MountState.
  MountState mount_state_ = {};
#else
  // Store start block + length for all extents. These may differ from info block for
  // sparse files.
  BlockOffsets offsets_;
#endif

  TransactionLimits limits_;
  MountOptions mount_options_;
};

#ifdef __Fuchsia__
// Replay the minfs journal, given the sizes provided within the superblock.
zx_status_t ReplayJournal(Bcache* bc, const Superblock& info, fs::JournalSuperblock* out);
#endif

// write the inode data of this vnode to disk (default does not update time values)
void SyncVnode(fbl::RefPtr<VnodeMinfs> vn, uint32_t flags);
void DumpInfo(const Superblock& info);
void DumpInode(const Inode* inode, ino_t ino);
zx_time_t GetTimeUTC();
void InitializeDirectory(void* bdata, ino_t ino_self, ino_t ino_parent);

// Given an input bcache, initialize the filesystem and return a reference to the
// root node.
zx_status_t Mount(std::unique_ptr<minfs::Bcache> bc, const MountOptions& options,
                  fbl::RefPtr<VnodeMinfs>* root_out);
}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_MINFS_PRIVATE_H_
