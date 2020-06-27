// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resizeable_vmo_buffer.h"

namespace minfs {

zx_status_t ResizeableVmoBuffer::Attach(
    const char* name, storage::VmoidRegistry* device) {
  ZX_DEBUG_ASSERT(!vmoid_.IsAttached());
  zx_status_t status = vmo_.CreateAndMap(block_size_, name);
  if (status != ZX_OK)
    return status;
  return device->BlockAttachVmo(vmo_.vmo(), &vmoid_);
}

zx_status_t ResizeableVmoBuffer::Detach(storage::VmoidRegistry* device) {
  return device->BlockDetachVmo(std::move(vmoid_));
}

void ResizeableVmoBuffer::Zero(size_t index, size_t count) {
  ZX_ASSERT(vmo_.vmo().op_range(ZX_VMO_OP_ZERO, index * BlockSize(),
                                count * BlockSize(), nullptr, 0) == ZX_OK);
}

}  // namespace minfs
