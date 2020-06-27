// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <lib/fit/variant.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// TODO(kulakowski) Design zx_status_t error values.

namespace {

struct Position {
  uint8_t* addr;
  Position operator+(uint32_t size) const { return Position{addr + size}; }
  Position& operator+=(uint32_t size) {
    addr += size;
    return *this;
  }
  template <typename T>
  constexpr T* Get() const {
    return reinterpret_cast<T*>(addr);
  }
};

struct EnvelopeCheckpoint {
  uint32_t num_bytes;
  uint32_t num_handles;
};

constexpr zx_rights_t subtract_rights(zx_rights_t minuend, zx_rights_t subtrahend) {
  return minuend & ~subtrahend;
}
static_assert(subtract_rights(0b011, 0b101) == 0b010, "ensure rights subtraction works correctly");

class FidlDecoder final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, Position, EnvelopeCheckpoint> {
 public:
  FidlDecoder(void* bytes, uint32_t num_bytes, const zx_handle_t* handles, uint32_t num_handles,
              uint32_t next_out_of_line, const char** out_error_msg)
      : bytes_(static_cast<uint8_t*>(bytes)),
        num_bytes_(num_bytes),
        num_handles_(num_handles),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {
    if (likely(handles != nullptr)) {
      handles_ = handles;
    }
  }

  FidlDecoder(void* bytes, uint32_t num_bytes, const zx_handle_info_t* handle_infos,
              uint32_t num_handle_infos, uint32_t next_out_of_line, const char** out_error_msg)
      : bytes_(static_cast<uint8_t*>(bytes)),
        num_bytes_(num_bytes),
        num_handles_(num_handle_infos),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {
    if (likely(handle_infos != nullptr)) {
      handles_ = handle_infos;
    }
  }

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = false;

  Status VisitAbsentPointerInNonNullableCollection(ObjectPointerPointer object_ptr_ptr) {
    SetError("absent pointer disallowed in non-nullable collection");
    return Status::kConstraintViolationError;
  }

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    uint32_t new_offset;
    if (unlikely(!FidlAddOutOfLine(next_out_of_line_, inline_size, &new_offset))) {
      SetError("overflow updating out-of-line offset");
      return Status::kMemoryError;
    }
    if (unlikely(new_offset > num_bytes_)) {
      SetError("message tried to decode more than provided number of bytes");
      return Status::kMemoryError;
    }
    {
      auto status = ValidatePadding(&bytes_[next_out_of_line_ + inline_size],
                                    new_offset - next_out_of_line_ - inline_size);
      if (status != Status::kSuccess) {
        return status;
      }
    }
    if (unlikely(pointee_type == PointeeType::kString)) {
      auto status = fidl_validate_string(reinterpret_cast<const char*>(&bytes_[next_out_of_line_]),
                                         inline_size);
      if (status != ZX_OK) {
        SetError("decoder encountered invalid UTF8 string");
        return Status::kConstraintViolationError;
      }
    }
    *out_position = Position{bytes_ + next_out_of_line_};
    *object_ptr_ptr = reinterpret_cast<void*>(&bytes_[next_out_of_line_]);

    next_out_of_line_ = new_offset;
    return Status::kSuccess;
  }

  Status VisitHandleInfo(Position handle_position, HandlePointer handle,
                         zx_rights_t required_handle_rights,
                         zx_obj_type_t required_handle_subtype) {
    assert(has_handle_infos());
    zx_handle_info_t received_handle_info = handle_infos()[handle_idx_];
    zx_handle_t received_handle = received_handle_info.handle;
    if (unlikely(received_handle == ZX_HANDLE_INVALID)) {
      SetError("invalid handle detected in handle table");
      return Status::kConstraintViolationError;
    }

    if (unlikely(required_handle_subtype != received_handle_info.type &&
                 required_handle_subtype != ZX_OBJ_TYPE_NONE)) {
      SetError("decoded handle object type does not match expected type");
      return Status::kConstraintViolationError;
    }

    // Special case: ZX_HANDLE_SAME_RIGHTS allows all handles through unchanged.
    if (required_handle_rights == ZX_RIGHT_SAME_RIGHTS) {
      *handle = received_handle;
      handle_idx_++;
      return Status::kSuccess;
    }
    // Check for required rights that are not present on the received handle.
    if (unlikely(subtract_rights(required_handle_rights, received_handle_info.rights) != 0)) {
      SetError("decoded handle missing required rights");
      return Status::kConstraintViolationError;
    }
    // Check for non-requested rights that are present on the received handle.
    if (subtract_rights(received_handle_info.rights, required_handle_rights)) {
#ifdef __Fuchsia__
      // The handle has more rights than required. Reduce the rights.
      zx_status_t status =
          zx_handle_replace(received_handle_info.handle, required_handle_rights, &received_handle);
      assert(status != ZX_ERR_BAD_HANDLE);
      if (unlikely(status != ZX_OK)) {
        SetError("failed to replace handle");
        return Status::kConstraintViolationError;
      }
#else
      SetError("more rights received than required");
      return Status::kConstraintViolationError;
#endif
    }
    *handle = received_handle;
    handle_idx_++;
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle,
                     zx_rights_t required_handle_rights, zx_obj_type_t required_handle_subtype) {
    if (unlikely(*handle != FIDL_HANDLE_PRESENT)) {
      SetError("message tried to decode a garbage handle");
      return Status::kConstraintViolationError;
    }
    if (unlikely(handle_idx_ == num_handles_)) {
      SetError("message decoded too many handles");
      return Status::kConstraintViolationError;
    }

    if (has_handles()) {
      if (unlikely(handles()[handle_idx_] == ZX_HANDLE_INVALID)) {
        SetError("invalid handle detected in handle table");
        return Status::kConstraintViolationError;
      }
      *handle = handles()[handle_idx_];
      handle_idx_++;
      return Status::kSuccess;
    } else if (likely(has_handle_infos())) {
      return VisitHandleInfo(handle_position, handle, required_handle_rights,
                             required_handle_subtype);
    } else {
      SetError("decoder noticed a handle is present but the handle table is empty");
      *handle = ZX_HANDLE_INVALID;
      return Status::kConstraintViolationError;
    }
  }

  Status VisitVectorOrStringCount(CountPointer ptr) { return Status::kSuccess; }

  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    auto padding_ptr = padding_position.template Get<const uint8_t>();
    return ValidatePadding(padding_ptr, padding_length);
  }

  EnvelopeCheckpoint EnterEnvelope() {
    return {
        .num_bytes = next_out_of_line_,
        .num_handles = handle_idx_,
    };
  }

  Status LeaveEnvelope(EnvelopePointer envelope, EnvelopeCheckpoint prev_checkpoint) {
    // Now that the envelope has been consumed, check the correctness of the envelope header.
    uint32_t num_bytes = next_out_of_line_ - prev_checkpoint.num_bytes;
    uint32_t num_handles = handle_idx_ - prev_checkpoint.num_handles;
    if (unlikely(envelope->num_bytes != num_bytes)) {
      SetError("Envelope num_bytes was mis-sized");
      return Status::kConstraintViolationError;
    }
    if (unlikely(envelope->num_handles != num_handles)) {
      SetError("Envelope num_handles was mis-sized");
      return Status::kConstraintViolationError;
    }
    return Status::kSuccess;
  }

  Status VisitUnknownEnvelope(EnvelopePointer envelope) {
    // If we do not have the coding table for this payload,
    // treat it as unknown and close its contained handles
    if (unlikely(envelope->num_handles > 0)) {
      if (has_handles()) {
        memcpy(&unknown_handles_[unknown_handle_idx_], &handles()[handle_idx_],
               envelope->num_handles * sizeof(zx_handle_t));
        handle_idx_ += envelope->num_handles;
        unknown_handle_idx_ += envelope->num_handles;
      } else if (has_handle_infos()) {
        uint32_t end = handle_idx_ + envelope->num_handles;
        for (; handle_idx_ < end; handle_idx_++, unknown_handle_idx_++) {
          unknown_handles_[unknown_handle_idx_] = handle_infos()[handle_idx_].handle;
        }
      }
    }

    return Status::kSuccess;
  }

  void OnError(const char* error) { SetError(error); }

  zx_status_t status() const { return status_; }

  bool DidConsumeAllBytes() const { return next_out_of_line_ == num_bytes_; }

  bool DidConsumeAllHandles() const { return handle_idx_ == num_handles_; }

  uint32_t unknown_handle_idx() const { return unknown_handle_idx_; }

  const zx_handle_t* unknown_handles() const { return unknown_handles_; }

 private:
  void SetError(const char* error) {
    if (status_ != ZX_OK) {
      return;
    }
    status_ = ZX_ERR_INVALID_ARGS;
    if (!out_error_msg_) {
      return;
    }
    *out_error_msg_ = error;
  }

  Status ValidatePadding(const uint8_t* padding_ptr, uint32_t padding_length) {
    for (uint32_t i = 0; i < padding_length; i++) {
      if (unlikely(padding_ptr[i] != 0)) {
        SetError("non-zero padding bytes detected during decoding");
        return Status::kConstraintViolationError;
      }
    }
    return Status::kSuccess;
  }

  bool has_handles() const { return fit::holds_alternative<const zx_handle_t*>(handles_); }
  bool has_handle_infos() const {
    return fit::holds_alternative<const zx_handle_info_t*>(handles_);
  }
  const zx_handle_t* handles() const { return fit::get<const zx_handle_t*>(handles_); }
  const zx_handle_info_t* handle_infos() const {
    return fit::get<const zx_handle_info_t*>(handles_);
  }

  // Message state passed in to the constructor.
  uint8_t* const bytes_;
  const uint32_t num_bytes_;
  fit::variant<fit::monostate, const zx_handle_t*, const zx_handle_info_t*> handles_;
  const uint32_t num_handles_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;

  // Decoder state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
  uint32_t unknown_handle_idx_ = 0;
  zx_handle_t unknown_handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
};

template <typename HandleType>
zx_status_t fidl_decode_impl(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                             const HandleType* handles, uint32_t num_handles,
                             const char** out_error_msg,
                             void (*close_handles)(const HandleType*, uint32_t)) {
  auto drop_all_handles = [&]() { close_handles(handles, num_handles); };
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (unlikely(handles == nullptr && num_handles != 0)) {
    set_error("Cannot provide non-zero handle count and null handle pointer");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(bytes == nullptr)) {
    set_error("Cannot decode null bytes");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(!FidlIsAligned(reinterpret_cast<uint8_t*>(bytes)))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t next_out_of_line;
  zx_status_t status;
  if (unlikely((status = fidl::StartingOutOfLineOffset(type, num_bytes, &next_out_of_line,
                                                       out_error_msg)) != ZX_OK)) {
    drop_all_handles();
    return status;
  }

  FidlDecoder decoder(bytes, num_bytes, handles, num_handles, next_out_of_line, out_error_msg);
  fidl::Walk(decoder, type, Position{reinterpret_cast<uint8_t*>(bytes)});

  if (unlikely(decoder.status() != ZX_OK)) {
    drop_all_handles();
    return decoder.status();
  }
  if (unlikely(!decoder.DidConsumeAllBytes())) {
    set_error("message did not decode all provided bytes");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(!decoder.DidConsumeAllHandles())) {
    set_error("message did not decode all provided handles");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

#ifdef __Fuchsia__
  if (unlikely(decoder.unknown_handle_idx() > 0)) {
    (void)zx_handle_close_many(decoder.unknown_handles(), decoder.unknown_handle_idx());
  }
#endif
  return ZX_OK;
}

void close_handles_op(const zx_handle_t* handles, uint32_t max_idx) {
#ifdef __Fuchsia__
  if (handles) {
    // Return value intentionally ignored. This is best-effort cleanup.
    zx_handle_close_many(handles, max_idx);
  }
#endif
}

void close_handle_infos_op(const zx_handle_info_t* handle_infos, uint32_t max_idx) {
#ifdef __Fuchsia__
  if (handle_infos) {
    zx_handle_t* handles = reinterpret_cast<zx_handle_t*>(alloca(sizeof(zx_handle_t) * max_idx));
    for (uint32_t i = 0; i < max_idx; i++) {
      handles[i] = handle_infos[i].handle;
    }
    // Return value intentionally ignored. This is best-effort cleanup.
    zx_handle_close_many(handles, max_idx);
  }
#endif
}

}  // namespace

zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** out_error_msg) {
  return fidl_decode_impl<zx_handle_t>(type, bytes, num_bytes, handles, num_handles, out_error_msg,
                                       close_handles_op);
}

zx_status_t fidl_decode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            const zx_handle_info_t* handle_infos, uint32_t num_handle_infos,
                            const char** out_error_msg) {
  return fidl_decode_impl<zx_handle_info_t>(type, bytes, num_bytes, handle_infos, num_handle_infos,
                                            out_error_msg, close_handle_infos_op);
}

zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_msg_t* msg, const char** out_error_msg) {
  return fidl_decode(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                     out_error_msg);
}
