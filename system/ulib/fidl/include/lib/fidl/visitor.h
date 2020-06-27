// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_VISITOR_H_
#define LIB_FIDL_VISITOR_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/internal_callable_traits.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace fidl {

struct NonMutatingVisitorTrait {
  // Types residing in the FIDL message buffer are const
  static constexpr bool kIsConst = true;

  // Message is const
  using ObjectPointerPointer = const void* const* const;
};

struct MutatingVisitorTrait {
  // Types residing in the FIDL message buffer are mutable
  static constexpr bool kIsConst = false;

  // Message is mutable
  using ObjectPointerPointer = void** const;
};

namespace {

// The interface of a FIDL message visitor.
//
// The walker class drives the message traversal, and encoders/decoders/validators etc.
// implement this interface to perform their task.
//
// Visitors should inherit from this class, which has compile-time checks that all visitor interface
// requirements have been met. The walker logic is always parameterized by a concrete implementation
// of this interface, hence there is no virtual method call overhead. MutationTrait is one of
// NonMutatingVisitorTrait or MutatingVisitorTrait.
//
// Many FIDL types do not need special treatment when encoding/decoding. Those that do include:
// - Handles: Transferred to/from handle table.
// - Indirections e.g. nullable fields, strings, vectors: Perform pointer patching.
//
// All pointers passed to the visitor are guaranteed to be alive throughout the duration
// of the message traversal.
// For all callbacks in the visitor, the return value indicates if an error has occurred.
template <typename MutationTrait_, typename Position_, typename EnvelopeCheckpoint_>
class Visitor {
 public:
  using MutationTrait = MutationTrait_;

  template <typename T>
  using Ptr = typename std::conditional<MutationTrait::kIsConst, typename std::add_const<T>::type,
                                        T>::type*;

  // A type encapsulating the position of the walker within the message. This type is parametric,
  // such that the walker does not assume any memory order between objects. |Position| is tracked
  // by the walker at every level of the coding frame, hence we encourage using a smaller type
  // for |Position|, and placing larger immutable values in |StartingPoint|. For example, in the
  // encoder, |StartingPoint| can be a 64-bit buffer address, while |Position| is a 32-bit offset.
  //
  // Implementations must have the following:
  // - Position operator+(uint32_t size) const, to advance position by |size| in the message.
  // - Position& operator+=(uint32_t size), to advance position by |size| in the message.
  // - template <typename T> Ptr<T> Get(StartingPoint start) const, to cast to a suitable pointer.
  using Position = Position_;

  // A type representing a checkpoint of the current state of the visitor at the time that the
  // envelope is entered. When the envelope is left, this value is given back to the visitor.
  // A common implementation is a struct with the number of already-processed bytes and handles.
  using EnvelopeCheckpoint = EnvelopeCheckpoint_;

  // ObjectPointerPointer is ([const] void*) *[const]
  using ObjectPointerPointer = typename MutationTrait::ObjectPointerPointer;

  // HandlePointer is ([const] zx_handle_t)*
  using HandlePointer = Ptr<zx_handle_t>;

  // EnvelopePointer is ([const] fidl_envelope_t)*
  using EnvelopePointer = Ptr<fidl_envelope_t>;

  // CountPointer is ([const] uint64_t)*
  using CountPointer = Ptr<uint64_t>;

  // Status returned by visitor callbacks.
  enum class Status {
    kSuccess = 0,
    kConstraintViolationError,  // recoverable errors
    kMemoryError                // overflow/out-of-bounds etc. Non-recoverable.
  };

  enum class PointeeType { kVector, kString, kOther };

  // Compile-time interface checking. Code is invisible to the subclass.
 private:
  // Visit an indirection, which can be the data pointer of a string/vector, the data pointer
  // of an envelope from a table, the pointer in a nullable type, etc.
  //
  // This will only be called when the pointer is present / non-null.
  //
  // |ptr_position|   Position of the pointer.
  // |pointee_type|   Type of the pointee.
  // |object_ptr_ptr| Pointer to the data pointer, obtained from |ptr_position.Get(start)|.
  //                  It can be used to patch the pointer.
  // |inline_size|    Size of the inline part of the target object.
  //                  For vectors, this covers the inline part of all the elements.
  //                  It will not contain any trailing padding between objects.
  // |out_position|   Returns the position where the walker will continue its object traversal.
  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    __builtin_unreachable();
  }

  // Visit a null/absent pointer in a collection that is normally non-nullable.
  //
  // The original intent of this method is to handle linearization of null data portions of
  // empty LLCPP vectors and strings.
  //
  // |object_ptr_ptr| Pointer to the data pointer, obtained from |ptr_position.Get(start)|.
  //                  It can be used to patch the pointer.
  Status VisitAbsentPointerInNonNullableCollection(ObjectPointerPointer object_ptr_ptr) {
    __builtin_unreachable();
  }

  // Visit a handle. The handle pointer will be mutable if the visitor is mutating.
  // Only called when the handle is present.
  // The handle pointer is derived from |handle_position.Get(start)|.
  Status VisitHandle(Position handle_position, HandlePointer handle_ptr, zx_rights_t handle_rights,
                     zx_obj_type_t handle_subtype) {
    __builtin_unreachable();
  }

  // Visit a vector or string count. The count pointer will be mutable if the visitor is mutating.
  Status VisitVectorOrStringCount(CountPointer ptr) { __builtin_unreachable(); }

  // Visit a region of padding bytes within message objects. They may be between members of a
  // struct, from after the last member to the end of the struct, or from after a union variant
  // to the end of a union. They should be zero on the wire.
  //
  // N.B. A different type of paddings exist between out-of-line message objects, which are always
  // aligned to |FIDL_ALIGNMENT|. They should be handled accordingly as part of |VisitPointer|.
  //
  // |padding_position| Position of the start of the padding region.
  // |padding_length|   Size of the padding region. It is always positive.
  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    __builtin_unreachable();
  }

  // Called when the walker encounters an envelope. The envelope may be empty or unknown.
  //
  // The visitor can return a checkpoint of its current state that is untouched by the walker
  // other than to hand back to the visitor when the envelope is exited.
  // Typically this checkpoint would include counts of number of bytes and handles processed,
  // but it can have arbitrary value or even be empty.
  EnvelopeCheckpoint EnterEnvelope() { __builtin_unreachable(); }

  // Called when the walker leaves an envelope.
  //
  // |envelope| is a pointer to the fidl_envelope_t structure containing the envelope.
  // |prev_checkpoint| is the checkpoint object returned in the EnterEnvelope call().
  Status LeaveEnvelope(EnvelopePointer envelope, EnvelopeCheckpoint prev_checkpoint) {
    __builtin_unreachable();
  }

  // Called when the walker encounters an envelope with unknown type that has non-null data.
  // This takes the place of the continued walk of the internal object that would take place
  // if they type was known.
  //
  // |envelope| is a pointer to the fidl_envelope_t structure containing the envelope.
  Status VisitUnknownEnvelope(EnvelopePointer envelope) { __builtin_unreachable(); }

  // Called when a traversal error is encountered on the walker side.
  void OnError(const char* error) {}

  template <typename Visitor_, typename ImplSubType_>
  friend constexpr bool CheckVisitorInterface();
};

template <typename Visitor, typename ImplSubType>
constexpr bool CheckVisitorInterface() {
  static_assert(std::is_base_of<Visitor, ImplSubType>::value,
                "ImplSubType should inherit from fidl::Visitor");

  // kContinueAfterConstraintViolation:
  // - When true, the walker will continue when constraints (e.g. string length) are violated.
  // - When false, the walker will stop upon first error of any kind.
  static_assert(
      std::is_same<decltype(ImplSubType::kContinueAfterConstraintViolation), const bool>::value,
      "ImplSubType must declare constexpr bool kContinueAfterConstraintViolation");

  static_assert(
      internal::SameInterface<decltype(&Visitor::VisitAbsentPointerInNonNullableCollection),
                              decltype(&ImplSubType::VisitAbsentPointerInNonNullableCollection)>,
      "Incorrect/missing VisitAbsentPointerInNonNullableCollection");
  static_assert(internal::SameInterface<decltype(&Visitor::VisitPointer),
                                        decltype(&ImplSubType::VisitPointer)>,
                "Incorrect/missing VisitPointer");
  static_assert(
      internal::SameInterface<decltype(&Visitor::VisitHandle), decltype(&ImplSubType::VisitHandle)>,
      "Incorrect/missing VisitHandle");
  static_assert(internal::SameInterface<decltype(&Visitor::VisitInternalPadding),
                                        decltype(&ImplSubType::VisitInternalPadding)>,
                "Incorrect/missing VisitInternalPadding");
  static_assert(internal::SameInterface<decltype(&Visitor::EnterEnvelope),
                                        decltype(&ImplSubType::EnterEnvelope)>,
                "Incorrect/missing EnterEnvelope");
  static_assert(internal::SameInterface<decltype(&Visitor::LeaveEnvelope),
                                        decltype(&ImplSubType::LeaveEnvelope)>,
                "Incorrect/missing LeaveEnvelope");
  static_assert(internal::SameInterface<decltype(&Visitor::VisitUnknownEnvelope),
                                        decltype(&ImplSubType::VisitUnknownEnvelope)>,
                "Incorrect/missing VisitUnknownEnvelope");
  static_assert(
      internal::SameInterface<decltype(&Visitor::OnError), decltype(&ImplSubType::OnError)>,
      "Incorrect/missing OnError");
  return true;
}

}  // namespace

}  // namespace fidl

#endif  // LIB_FIDL_VISITOR_H_
