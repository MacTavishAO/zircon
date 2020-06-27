// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_INSPECTOR_TRANSACTION_HANDLER_H_
#define DISK_INSPECTOR_INSPECTOR_TRANSACTION_HANDLER_H_

#include <lib/fit/result.h>
#include <zircon/types.h>

#include <memory>

#include <block-client/cpp/block-device.h>
#include <fs/transaction/legacy_transaction_handler.h>
#include <storage/buffer/block_buffer.h>
#include <storage/buffer/vmoid_registry.h>
#include <storage/operation/operation.h>

namespace disk_inspector {

// Vmo-based implementation of TransactionHandler for use with disk-inspect application.
class InspectorTransactionHandler : public fs::LegacyTransactionHandler, public storage::VmoidRegistry {
 public:
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device, uint32_t block_size,
                            std::unique_ptr<InspectorTransactionHandler>* out);

  InspectorTransactionHandler(const InspectorTransactionHandler&) = delete;
  InspectorTransactionHandler(InspectorTransactionHandler&&) = delete;
  InspectorTransactionHandler& operator=(const InspectorTransactionHandler&) = delete;
  InspectorTransactionHandler& operator=(InspectorTransactionHandler&&) = delete;
  ~InspectorTransactionHandler() override = default;

  // fs::TransactionHandler interface:
  uint64_t BlockNumberToDevice(uint64_t block_num) const final;
  block_client::BlockDevice* GetDevice() final { return device_.get(); }

  // storage::VmoidRegistry interface:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;

 private:
  explicit InspectorTransactionHandler(std::unique_ptr<block_client::BlockDevice> device,
                                       fuchsia_hardware_block_BlockInfo info, uint32_t block_size)
      : device_(std::move(device)), info_(info), block_size_(block_size) {}

  // TransactionHandler interface scheduled for removal:
  uint32_t FsBlockSize() const final { return block_size_; }
  uint32_t DeviceBlockSize() const final { return info_.block_size; }
  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::unique_ptr<block_client::BlockDevice> device_;
  fuchsia_hardware_block_BlockInfo info_ = {};
  uint32_t block_size_;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_INSPECTOR_TRANSACTION_HANDLER_H_
