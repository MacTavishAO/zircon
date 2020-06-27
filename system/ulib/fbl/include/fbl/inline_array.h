// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_INLINE_ARRAY_H_
#define ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_INLINE_ARRAY_H_

#include <stddef.h>
#include <zircon/assert.h>

#include <new>

#include <fbl/alloc_checker.h>
#include <fbl/macros.h>

namespace fbl {

// Runtime-determined, fixed size arrays that are "inlined" (e.g., on the stack) if the size at most
// |max_inline_count| or heap-allocated otherwise. This is typically used like:
//
//   fbl::AllocChecker ac;
//   fbl::InlineArray<zx_handle_t, 4u> handle_values(&ac, num_handles);
//   if (!ac.check())
//       return ZX_ERR_NO_MEMORY;
//
// Note: Currently, |max_inline_count| must be at least 1.
template <typename T, size_t max_inline_count>
class __OWNER(T) InlineArray {
 public:
  InlineArray(fbl::AllocChecker* ac, size_t count)
      : count_(count),
        ptr_(!count_ ? nullptr
                     : is_inline() ? reinterpret_cast<T*>(inline_storage_) : new (ac) T[count_]) {
    if (is_inline()) {
      // Arm the AllocChecker even if we didn't allocate -- the user should check it
      // regardless!
      ac->arm(0u, true);
      for (size_t i = 0; i < count_; i++)
        new (&ptr_[i]) T();
    }
  }

  ~InlineArray() {
    if (is_inline()) {
      for (size_t i = 0; i < count_; i++)
        ptr_[i].~T();
    } else {
      delete[] ptr_;
    }
  }

  InlineArray() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(InlineArray);

  size_t size() const { return count_; }

  T* get() const { return ptr_; }

  T& operator[](size_t i) const {
    ZX_DEBUG_ASSERT(i < count_);
    return ptr_[i];
  }

 private:
  bool is_inline() const { return count_ <= max_inline_count; }

  const size_t count_;
  T* const ptr_;
  alignas(T) char inline_storage_[max_inline_count * sizeof(T)];
};

}  // namespace fbl

#endif  // ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_INLINE_ARRAY_H_
