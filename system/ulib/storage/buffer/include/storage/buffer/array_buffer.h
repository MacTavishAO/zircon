// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_ARRAY_BUFFER_H_
#define STORAGE_BUFFER_ARRAY_BUFFER_H_

#include <vector>

#include <storage/buffer/block_buffer.h>

namespace storage {

// Block buffer backed by a heap array.
class ArrayBuffer : public BlockBuffer {
 public:
  explicit ArrayBuffer(size_t capacity, uint32_t block_size);
  ArrayBuffer(const ArrayBuffer&) = delete;
  ArrayBuffer(ArrayBuffer&&) = default;
  ArrayBuffer& operator=(const ArrayBuffer&) = delete;
  ArrayBuffer& operator=(ArrayBuffer&&) = default;
  ~ArrayBuffer() = default;

  // BlockBuffer interface:
  size_t capacity() const final { return buffer_.size() / block_size_; }
  uint32_t BlockSize() const final { return block_size_; }
  vmoid_t vmoid() const final { return BLOCK_VMOID_INVALID; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final;
  const void* Data(size_t index) const final;

 protected:
  std::vector<uint8_t>& buffer() { return buffer_; }

 private:
  std::vector<uint8_t> buffer_;
  uint32_t block_size_ = 0;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_ARRAY_BUFFER_H_
