// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_trace_manager.h"

#include <fuchsia/tracing/provider/c/fidl.h>
#include <lib/fidl/coding.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <utility>

namespace trace {
namespace test {

void FakeTraceManager::Create(async_dispatcher_t* dispatcher,
                              std::unique_ptr<FakeTraceManager>* out_manager,
                              zx::channel* out_channel) {
  zx::channel server, client;
  zx_status_t status = zx::channel::create(0, &server, &client);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  auto manager = new FakeTraceManager(dispatcher, std::move(server));
  out_manager->reset(manager);
  *out_channel = std::move(client);
}

FakeTraceManager::FakeTraceManager(async_dispatcher_t* dispatcher, zx::channel channel)
    : channel_(std::move(channel)),
      wait_(this, channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {
  zx_status_t status = wait_.Begin(dispatcher);
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void FakeTraceManager::Close() { channel_.reset(); }

void FakeTraceManager::Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    // The wait could be canceled if we're shutting down, e.g., the
    // program is exiting.
    return;
  }

  if (status != ZX_OK) {
    fprintf(stderr, "FakeTraceManager: wait failed: %d(%s)\n", status,
            zx_status_get_string(status));
  } else if (signal->observed & ZX_CHANNEL_READABLE) {
    if (ReadMessage()) {
      zx_status_t status = wait_.Begin(dispatcher);
      if (status == ZX_OK) {
        return;
      }
      fprintf(stderr, "FakeTraceManager: Error re-registering channel wait: %d(%s)\n", status,
              zx_status_get_string(status));
    } else {
      fprintf(stderr, "FakeTraceManager: received invalid FIDL message or failed to send reply\n");
    }
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  }

  Close();
}

bool FakeTraceManager::ReadMessage() {
  FIDL_ALIGNDECL uint8_t buffer[16 * 1024];
  uint32_t num_bytes = 0u;
  constexpr uint32_t kNumHandles = 2;
  zx_handle_t handles[kNumHandles];
  uint32_t num_handles = 0u;
  zx_status_t status =
      channel_.read(0u, buffer, handles, sizeof(buffer), kNumHandles, &num_bytes, &num_handles);
  if (status != ZX_OK) {
    fprintf(stderr, "FakeTraceManager: channel read failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    return false;
  }

  if (!DecodeAndDispatch(buffer, num_bytes, handles, num_handles)) {
    fprintf(stderr, "FakeTraceManager: DecodeAndDispatch failed\n");
    zx_handle_close_many(handles, num_handles);
    return false;
  }

  return true;
}

bool FakeTraceManager::DecodeAndDispatch(uint8_t* buffer, uint32_t num_bytes, zx_handle_t* handles,
                                         uint32_t num_handles) {
  printf("FakeTraceManager: Got request\n");

  if (num_bytes < sizeof(fidl_message_header_t)) {
    return false;
  }

  auto hdr = reinterpret_cast<fidl_message_header_t*>(buffer);
  uint64_t ordinal = hdr->ordinal;
  switch (ordinal) {
    case fuchsia_tracing_provider_RegistryRegisterProviderOrdinal:
      printf("FakeTraceManager: Got RegisterProvider request\n");
      // We currently don't need to do anything more here.
      return true;
    default:
      return false;
  }
}

}  // namespace test
}  // namespace trace
