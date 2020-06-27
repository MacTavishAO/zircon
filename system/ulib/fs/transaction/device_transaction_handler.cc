// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/transaction/device_transaction_handler.h>

namespace fs {

zx_status_t DeviceTransactionHandler::RunRequests(
    const std::vector<storage::BufferedOperation>& operations) {
  if (operations.empty()) {
    return ZX_OK;
  }

  // Update all the outgoing transactions to be in disk blocks.
  std::vector<block_fifo_request_t> block_requests(operations.size());
  for (size_t i = 0; i < operations.size(); i++) {
    auto& request = block_requests[i];
    request.vmoid = operations[i].vmoid;

    const auto& operation = operations[i].op;
    switch (operation.type) {
      case storage::OperationType::kRead:
        request.opcode = BLOCKIO_READ;
        break;
      case storage::OperationType::kWrite:
        request.opcode = BLOCKIO_WRITE;
        break;
      case storage::OperationType::kTrim:
        request.opcode = BLOCKIO_TRIM;
        break;
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Unsupported operation");
    }
    // For the time being, restrict a transaction to operations of the same type.
    // This probably can be relaxed, as the concept of a transaction implies the
    // operations take place logically at the same time, so even if there's a
    // mix of reads and writes, it doesn't make sense to depend on the relative
    // order of the operations, which is what could break with the merging done
    // by the request builder.
    ZX_DEBUG_ASSERT(operation.type == operations[0].op.type);

    request.vmo_offset = BlockNumberToDevice(operation.vmo_offset);
    request.dev_offset = BlockNumberToDevice(operation.dev_offset);
    uint64_t length = BlockNumberToDevice(operation.length);
    if (length > std::numeric_limits<decltype(request.length)>::max()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    request.length = static_cast<decltype(request.length)>(length);
  }

  return GetDevice()->FifoTransaction(&block_requests[0], operations.size());
}

}  // namespace fs
