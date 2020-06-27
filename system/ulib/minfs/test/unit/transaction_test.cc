// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests Transaction behavior.

#include <zircon/assert.h>

#include <memory>

#include <minfs/writeback.h>
#include <zxtest/zxtest.h>

#include "minfs_private.h"
#include "unowned_vmo_buffer.h"

namespace minfs {
namespace {

constexpr size_t kTotalElements = 32768;
constexpr size_t kDefaultElements = kTotalElements / 64;
constexpr size_t kDefaultStartBlock = 0;

// Fake Storage class to be used in Transaction tests.
class FakeStorage : public AllocatorStorage {
 public:
  FakeStorage() = delete;
  FakeStorage(const FakeStorage&) = delete;
  FakeStorage& operator=(const FakeStorage&) = delete;

  FakeStorage(uint32_t units) : pool_used_(0), pool_total_(units) {}

  ~FakeStorage() {}

  zx_status_t AttachVmo(const zx::vmo& vmo, storage::OwnedVmoid* vmoid) final {
    return ZX_OK;
  }

  void Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) final {}

  zx_status_t Extend(PendingWork* transaction, WriteData data, GrowMapCallback grow_map) final {
    return ZX_ERR_NO_SPACE;
  }

  uint32_t PoolAvailable() const final { return pool_total_ - pool_used_; }

  uint32_t PoolTotal() const final { return pool_total_; }

  // Write back the allocation of the following items to disk.
  void PersistRange(PendingWork* transaction, WriteData data, size_t index, size_t count) final {}

  void PersistAllocate(PendingWork* transaction, size_t count) final {
    ZX_DEBUG_ASSERT(pool_used_ + count <= pool_total_);
    pool_used_ += static_cast<uint32_t>(count);
  }

  void PersistRelease(PendingWork* transaction, size_t count) final {
    ZX_DEBUG_ASSERT(pool_used_ >= count);
    pool_used_ -= static_cast<uint32_t>(count);
  }

 private:
  uint32_t pool_used_;
  uint32_t pool_total_;
};

// Fake BlockDevice class to be used in Transaction tests.
class FakeBlockDevice : public block_client::BlockDevice {
 public:
  FakeBlockDevice() {}

  zx_status_t ReadBlock(uint64_t block_num, uint64_t block_size, void* block) const final {
    return ZX_OK;
  }
  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final { return ZX_OK; }
  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) const final {
    return ZX_OK;
  }
  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const final { return ZX_OK; }
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) final {
    return ZX_OK;
  }

  zx_status_t VolumeQuery(fuchsia_hardware_block_volume_VolumeInfo* out_info) const final {
    return ZX_OK;
  }
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const final {
    return ZX_OK;
  }
  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final { return ZX_OK; }
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final { return ZX_OK; }
};

// Mock Minfs class to be used in Transaction tests.
class FakeMinfs : public TransactionalFs {
 public:
  FakeMinfs() {
    info_.inode_count = kTotalElements;
  }

  fbl::Mutex* GetLock() const override { return &txn_lock_; }

  zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                               std::unique_ptr<Transaction>* out) override {
    return ZX_OK;
  }

  void EnqueueCallback(SyncCallback callback) override {}

  void CommitTransaction(std::unique_ptr<Transaction> transaction) override {}

  Bcache* GetMutableBcache() override { return nullptr; }

  Allocator& GetBlockAllocator() override {
    if (!block_allocator_) {
      std::unique_ptr<FakeStorage> storage(new FakeStorage(kTotalElements));
      ZX_ASSERT(Allocator::Create(&builder_, std::move(storage), &block_allocator_) == ZX_OK);
    }
    return *block_allocator_;
  }

  Allocator& GetInodeAllocator() override {
    return GetInodeManager().inode_allocator();
  }

  InodeManager& GetInodeManager() {
    if (!inode_manager_) {
      // Create superblock manager.
      ZX_ASSERT(SuperblockManager::Create(&block_device_, &info_, kDefaultStartBlock,
                                          IntegrityCheck::kNone, &superblock_manager_) == ZX_OK);

      // Create inode manager.
      AllocatorFvmMetadata fvm_metadata;
      AllocatorMetadata metadata(kDefaultStartBlock, kDefaultStartBlock, false,
                                 std::move(fvm_metadata), superblock_manager_.get(),
                                 SuperblockAllocatorAccess::Inodes());
      ZX_ASSERT(InodeManager::Create(&block_device_, superblock_manager_.get(), &builder_,
                                     std::move(metadata), kDefaultStartBlock, kTotalElements,
                                     &inode_manager_) == ZX_OK);
    }
    return *inode_manager_;
  }

  zx_status_t CreateTransaction(size_t inodes, size_t blocks, std::unique_ptr<Transaction>* out) {
    return Transaction::Create(this, inodes, blocks, &GetInodeManager(), out);
  }

 private:
  mutable fbl::Mutex txn_lock_;
  FakeBlockDevice block_device_;
  fs::BufferedOperationsBuilder builder_;
  Superblock info_ = {};
  std::unique_ptr<SuperblockManager> superblock_manager_;
  std::unique_ptr<InodeManager> inode_manager_;
  std::unique_ptr<Allocator> block_allocator_;
};

// Creates a Transaction using the public constructor, which by default contains no reservations.
TEST(TransactionTest, CreateTransactionNoReservationsAlt) {
  FakeMinfs minfs;
  Transaction transaction(&minfs);
}

// Creates a Transaction with no reservations.
TEST(TransactionTest, CreateTransactionNoReservations) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(0, 0, &transaction));
}

// Creates a Transaction with inode and block reservations.
TEST(TransactionTest, CreateTransactionWithReservations) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(kDefaultElements, kDefaultElements, &transaction));
}

// Creates a Transaction with the maximum possible number of inodes and blocks reserved.
TEST(TransactionTest, CreateTransactionWithMaxBlockReservations) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(kTotalElements, kTotalElements, &transaction));
}

// Attempts to create a transaction with more than the maximum available inodes reserved.
TEST(TransactionTest, CreateTransactionTooManyInodesFails) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_EQ(ZX_ERR_NO_SPACE, minfs.CreateTransaction(kTotalElements + 1, 0, &transaction));
}

// Attempts to create a transaction with more than the maximum available blocks reserved.
TEST(TransactionTest, CreateTransactionTooManyBlocksFails) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_EQ(ZX_ERR_NO_SPACE, minfs.CreateTransaction(0, kTotalElements + 1, &transaction));
}

// Tests allocation of a single inode.
TEST(TransactionTest, InodeAllocationSucceeds) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(kDefaultElements, kDefaultElements, &transaction));
  ASSERT_NO_DEATH([&transaction]() { transaction->AllocateInode(); });
}

// Tests allocation of a single block.
TEST(TransactionTest, BlockAllocationSucceeds) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(kDefaultElements, kDefaultElements, &transaction));
  ASSERT_NO_DEATH([&transaction]() { transaction->AllocateBlock(); });
}

// Attempts to allocate an inode when the transaction was not initialized properly.
TEST(TransactionDeathTest, AllocateInodeWithoutInitializationFails) {
  FakeMinfs minfs;
  Transaction transaction(&minfs);
  ASSERT_DEATH([&transaction]() { transaction.AllocateInode(); });
}

// Attempts to allocate a block when the transaction was not initialized properly.
TEST(TransactionDeathTest, AllocateBlockWithoutInitializationFails) {
  FakeMinfs minfs;
  Transaction transaction(&minfs);
  ASSERT_DEATH([&transaction]() { transaction.AllocateBlock(); });
}

#if ZX_DEBUG_ASSERT_IMPLEMENTED
// Attempts to allocate an inode when none have been reserved.
TEST(TransactionDeathTest, AllocateTooManyInodesFails) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(1, 0, &transaction));

  // First allocation should succeed.
  ASSERT_NO_DEATH([&transaction]() { transaction->AllocateInode(); });

  // Second allocation should fail.
  ASSERT_DEATH([&transaction]() { transaction->AllocateInode(); });
}
#endif

#if ZX_DEBUG_ASSERT_IMPLEMENTED
// Attempts to allocate a block when none have been reserved.
TEST(TransactionDeathTest, AllocateTooManyBlocksFails) {
  FakeMinfs minfs;
  std::unique_ptr<Transaction> transaction;
  ASSERT_OK(minfs.CreateTransaction(0, 1, &transaction));

  // First allocation should succeed.
  ASSERT_NO_DEATH([&transaction]() { transaction->AllocateBlock(); });

  // Second allocation should fail.
  ASSERT_DEATH([&transaction]() { transaction->AllocateBlock(); });
}
#endif

// Checks that the Transaction's work is empty before any writes have been enqueued.
TEST(TransactionTest, VerifyNoWorkExistsBeforeEnqueue) {
  FakeMinfs minfs;
  Transaction transaction(&minfs);

  // Metadata operations should be empty.
  fbl::Vector<storage::UnbufferedOperation> meta_operations =
      transaction.RemoveMetadataOperations();
  ASSERT_TRUE(meta_operations.is_empty());

  // Data work should be empty.
  fbl::Vector<storage::UnbufferedOperation> data_operations = transaction.RemoveDataOperations();
  ASSERT_TRUE(data_operations.is_empty());
}

// Checks that the Transaction's metadata work is populated after enqueueing metadata writes.
TEST(TransactionTest, EnqueueAndVerifyMetadataWork) {
  FakeMinfs minfs;
  Transaction transaction(&minfs);

  storage::Operation op = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = 2,
      .dev_offset = 3,
      .length = 4,
  };
  UnownedVmoBuffer buffer(zx::unowned_vmo(1));
  transaction.EnqueueMetadata(op, &buffer);

  fbl::Vector<storage::UnbufferedOperation> meta_operations =
      transaction.RemoveMetadataOperations();
  ASSERT_EQ(1, meta_operations.size());
  ASSERT_EQ(1, meta_operations[0].vmo);
  ASSERT_EQ(2, meta_operations[0].op.vmo_offset);
  ASSERT_EQ(3, meta_operations[0].op.dev_offset);
  ASSERT_EQ(4, meta_operations[0].op.length);
  ASSERT_EQ(storage::OperationType::kWrite, meta_operations[0].op.type);
}

// Checks that the Transaction's data work is populated after enqueueing data writes.
TEST(TransactionTest, EnqueueAndVerifyDataWork) {
  FakeMinfs minfs;
  Transaction transaction(&minfs);

  storage::Operation op = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = 2,
      .dev_offset = 3,
      .length = 4,
  };
  UnownedVmoBuffer buffer(zx::unowned_vmo(1));
  transaction.EnqueueData(op, &buffer);

  fbl::Vector<storage::UnbufferedOperation> data_operations = transaction.RemoveDataOperations();
  ASSERT_EQ(1, data_operations.size());
  ASSERT_EQ(1, data_operations[0].vmo);
  ASSERT_EQ(2, data_operations[0].op.vmo_offset);
  ASSERT_EQ(3, data_operations[0].op.dev_offset);
  ASSERT_EQ(4, data_operations[0].op.length);
  ASSERT_EQ(storage::OperationType::kWrite, data_operations[0].op.type);
}

class MockVnodeMinfs : public VnodeMinfs, public fbl::Recyclable<MockVnodeMinfs> {
 public:
  MockVnodeMinfs(bool* alive) : VnodeMinfs(nullptr), alive_(alive) { *alive_ = true; }

  ~MockVnodeMinfs() { *alive_ = false; }

  // fbl::Recyclable interface.
  void fbl_recycle() final { delete this; }

 private:
  bool IsDirectory() const final { return false; }
  zx_status_t CanUnlink() const final { return false; }

  // minfs::Vnode interface.
  blk_t GetBlockCount() const final { return 0; }
  uint64_t GetSize() const final { return 0; }
  void SetSize(uint32_t new_size) final {}
  void AcquireWritableBlock(Transaction* transaction, blk_t local_bno, blk_t old_bno,
                            blk_t* out_bno) final {}
  void DeleteBlock(PendingWork* transaction, blk_t local_bno, blk_t old_bno, bool indirect) final {}
  void IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                      blk_t count) final {}
  bool HasPendingAllocation(blk_t vmo_offset) final { return false; }
  void CancelPendingWriteback() final {}

  // fs::Vnode interface.
  fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kFile; }
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final { return ZX_OK; }
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final {
    return ZX_OK;
  }
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final {
    return ZX_OK;
  }
  zx_status_t Truncate(size_t len) final { return ZX_OK; }

  bool* alive_;
};

// Checks that a pinned vnode is not attached to the transaction's data work.
TEST(TransactionTest, RemovePinnedVnodeContainsVnode) {
  FakeMinfs minfs;
  bool vnode_alive = false;

  fbl::RefPtr<MockVnodeMinfs> vnode(fbl::AdoptRef(new MockVnodeMinfs(&vnode_alive)));
  ASSERT_TRUE(vnode_alive);

  Transaction transaction(&minfs);
  transaction.PinVnode(std::move(vnode));
  ASSERT_EQ(nullptr, vnode);

  std::vector<fbl::RefPtr<VnodeMinfs>> pinned_vnodes = transaction.RemovePinnedVnodes();
  ASSERT_EQ(1, pinned_vnodes.size());

  pinned_vnodes.clear();
  ASSERT_FALSE(vnode_alive);
}

TEST(TransactionTest, RemovePinnedVnodeContainsManyVnodes) {
  FakeMinfs minfs;
  size_t vnode_count = 4;
  bool vnode_alive[vnode_count];
  fbl::RefPtr<MockVnodeMinfs> vnodes[vnode_count];
  Transaction transaction(&minfs);

  for (size_t i = 0; i < vnode_count; i++) {
    vnode_alive[i] = false;
    vnodes[i] = fbl::AdoptRef(new MockVnodeMinfs(&vnode_alive[i]));
    ASSERT_TRUE(vnode_alive[i]);
    transaction.PinVnode(std::move(vnodes[i]));
    ASSERT_EQ(nullptr, vnodes[i]);
  }

  std::vector<fbl::RefPtr<VnodeMinfs>> pinned_vnodes = transaction.RemovePinnedVnodes();
  ASSERT_EQ(vnode_count, pinned_vnodes.size());

  pinned_vnodes.clear();

  for (size_t i = 0; i < vnode_count; i++) {
    ASSERT_FALSE(vnode_alive[i]);
  }
}

}  // namespace
}  // namespace minfs
