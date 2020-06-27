// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_USER_PTR_H_
#define ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_USER_PTR_H_

#include <lib/user_copy/internal.h>
#include <stddef.h>
#include <zircon/types.h>

#include <arch/user_copy.h>
#include <ktl/type_traits.h>
#include <vm/vm.h>

// user_*_ptr<> wraps a pointer to user memory, to differentiate it from kernel
// memory. They can be in, out, or inout pointers.
//
// user_*_ptr<> ensure that types copied to/from usermode are ABI-safe (see |is_copy_allowed|).

namespace internal {

enum InOutPolicy {
  kIn = 1,
  kOut = 2,
  kInOut = kIn | kOut,
};

template <typename T, InOutPolicy Policy>
class user_ptr {
 public:
  using ValueType = ktl::remove_const_t<T>;

  static_assert(ktl::is_const<T>::value == (Policy == kIn),
                "In pointers must be const, and Out and InOut pointers must not be const.");

  explicit user_ptr(T* p) : ptr_(p) {}

  user_ptr(const user_ptr& other) : ptr_(other.ptr_) {}

  user_ptr& operator=(const user_ptr& other) {
    ptr_ = other.ptr_;
    return *this;
  }

  enum { is_out = ((Policy & kOut) == kOut) };

  T* get() const { return ptr_; }

  template <typename C>
  user_ptr<C, Policy> reinterpret() const {
    return user_ptr<C, Policy>(reinterpret_cast<C*>(ptr_));
  }

  // special operator to return the nullness of the pointer
  explicit operator bool() const { return ptr_ != nullptr; }

  // Returns a user_ptr pointing to the |index|-th element from this one, or a null user_ptr if
  // this pointer is null. Note: This does no other validation, and the behavior is undefined on
  // overflow. (Using this will fail to compile if T is |void|.)
  user_ptr element_offset(size_t index) const {
    return ptr_ ? user_ptr(ptr_ + index) : user_ptr(nullptr);
  }

  // Returns a user_ptr offset by |offset| bytes from this one.
  user_ptr byte_offset(size_t offset) const {
    return ptr_ ? user_ptr(reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr_) + offset))
                : user_ptr(nullptr);
  }

  // Copies a single T to user memory. T must not be |void|.
  template <typename S>
  [[nodiscard]] zx_status_t copy_to_user(const S& src) const {
    static_assert(!ktl::is_void<S>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(ktl::is_same<S, T>::value, "S and T must be the same type.");
    static_assert(is_copy_allowed<S>::value, "Type must be ABI-safe.");
    static_assert(Policy & kOut, "Can only copy to user for kOut or kInOut user_ptr.");
    return arch_copy_to_user(ptr_, &src, sizeof(S));
  }

  // Copies a single T to user memory. T must not be |void|.
  //
  // On success ZX_OK is returned and the values in pf_va and pf_flags are undefined, otherwise they
  // are filled with fault information.
  template <typename S>
  [[nodiscard]] UserCopyCaptureFaultsResult copy_to_user_capture_faults(const S& src) const {
    static_assert(!ktl::is_void<S>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(ktl::is_same<S, T>::value, "S and T must be the same type.");
    static_assert(is_copy_allowed<S>::value, "Type must be ABI-safe.");
    static_assert(Policy & kOut, "Can only copy to user for kOut or kInOut user_ptr.");
    return arch_copy_to_user_capture_faults(ptr_, &src, sizeof(S));
  }

  // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
  [[nodiscard]] zx_status_t copy_array_to_user(const T* src, size_t count) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kOut, "Can only copy to user for kOut or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return ZX_ERR_INVALID_ARGS;
    }
    return arch_copy_to_user(ptr_, src, len);
  }

  // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
  //
  // On success ZX_OK is returned and the values in pf_va and pf_flags are undefined, otherwise they
  // are filled with fault information.
  [[nodiscard]] UserCopyCaptureFaultsResult copy_array_to_user_capture_faults(const T* src,
                                                                              size_t count) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kOut, "Can only copy to user for kOut or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
    }
    return arch_copy_to_user_capture_faults(ptr_, src, len);
  }

  // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
  [[nodiscard]] zx_status_t copy_array_to_user(const T* src, size_t count, size_t offset) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kOut, "Can only copy to user for kOut or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return ZX_ERR_INVALID_ARGS;
    }
    return arch_copy_to_user(ptr_ + offset, src, len);
  }

  // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
  //
  // On success ZX_OK is returned and the values in pf_va and pf_flags are undefined, otherwise they
  // are filled with fault information.
  [[nodiscard]] UserCopyCaptureFaultsResult copy_array_to_user_capture_faults(const T* src,
                                                                              size_t count,
                                                                              size_t offset) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kOut, "Can only copy to user for kOut or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
    }
    return arch_copy_to_user_capture_faults(ptr_ + offset, src, len);
  }

  // Copies a single T from user memory. T must not be |void|.
  [[nodiscard]] zx_status_t copy_from_user(typename ktl::remove_const<T>::type* dst) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kIn, "Can only copy from user for kIn or kInOut user_ptr.");
    return arch_copy_from_user(dst, ptr_, sizeof(T));
  }

  // Copies a single T from user memory. T must not be |void|.
  //
  // On success ZX_OK is returned and the values in pf_va and pf_flags are undefined, otherwise they
  // are filled with fault information.
  [[nodiscard]] UserCopyCaptureFaultsResult copy_from_user_capture_faults(
      typename ktl::remove_const<T>::type* dst) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kIn, "Can only copy from user for kIn or kInOut user_ptr.");
    return arch_copy_from_user_capture_faults(dst, ptr_, sizeof(T));
  }

  // Copies an array of T from user memory. Note: This takes a count not a size, unless T is |void|.
  [[nodiscard]] zx_status_t copy_array_from_user(typename ktl::remove_const<T>::type* dst,
                                                 size_t count) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kIn, "Can only copy from user for kIn or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return ZX_ERR_INVALID_ARGS;
    }
    return arch_copy_from_user(dst, ptr_, len);
  }

  // Copies an array of T from user memory. Note: This takes a count not a size, unless T is |void|.
  //
  // On success ZX_OK is returned and the values in pf_va and pf_flags are undefined, otherwise they
  // are filled with fault information.
  [[nodiscard]] UserCopyCaptureFaultsResult copy_array_from_user_capture_faults(
      typename ktl::remove_const<T>::type* dst, size_t count) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kIn, "Can only copy from user for kIn or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
    }
    return arch_copy_from_user_capture_faults(dst, ptr_, len);
  }

  // Copies a sub-array of T from user memory. Note: This takes a count not a size, unless T is
  // |void|.
  [[nodiscard]] zx_status_t copy_array_from_user(typename ktl::remove_const<T>::type* dst,
                                                 size_t count, size_t offset) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kIn, "Can only copy from user for kIn or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return ZX_ERR_INVALID_ARGS;
    }
    return arch_copy_from_user(dst, ptr_ + offset, len);
  }

  // Copies a sub-array of T from user memory. Note: This takes a count not a size, unless T is
  // |void|.
  //
  // On success ZX_OK is returned and the values in pf_va and pf_flags are undefined, otherwise they
  // are filled with fault information.
  [[nodiscard]] UserCopyCaptureFaultsResult copy_array_from_user_capture_faults(
      typename ktl::remove_const<T>::type* dst, size_t count, size_t offset) const {
    static_assert(!ktl::is_void<T>::value, "Type cannot be void. Use .reinterpret<>().");
    static_assert(is_copy_allowed<T>::value, "Type must be ABI-safe.");
    static_assert(Policy & kIn, "Can only copy from user for kIn or kInOut user_ptr.");
    size_t len;
    if (mul_overflow(count, sizeof(T), &len)) {
      return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
    }
    return arch_copy_from_user_capture_faults(dst, ptr_ + offset, len);
  }

 private:
  // It is very important that this class only wrap the pointer type itself
  // and not include any other members so as not to break the ABI between
  // the kernel and user space.
  T* ptr_;
};

}  // namespace internal

template <typename T>
using user_in_ptr = internal::user_ptr<T, internal::kIn>;

template <typename T>
using user_out_ptr = internal::user_ptr<T, internal::kOut>;

template <typename T>
using user_inout_ptr = internal::user_ptr<T, internal::kInOut>;

template <typename T>
user_in_ptr<T> make_user_in_ptr(T* p) {
  return user_in_ptr<T>(p);
}

template <typename T>
user_out_ptr<T> make_user_out_ptr(T* p) {
  return user_out_ptr<T>(p);
}

template <typename T>
user_inout_ptr<T> make_user_inout_ptr(T* p) {
  return user_inout_ptr<T>(p);
}

#endif  // ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_USER_PTR_H_
