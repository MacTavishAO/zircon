// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/collector_internal.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {
namespace internal {
namespace {

internal::CobaltOptions MakeCobaltOptions(uint32_t project_id) {
  ZX_ASSERT_MSG(project_id > 0, "Must define a project_id greater than 0.");
  internal::CobaltOptions cobalt_options;
  cobalt_options.project_id = project_id;
  cobalt_options.service_connect = [](const char* service_path,
                                      zx::channel service) -> zx_status_t {
    return fdio_service_connect(service_path, service.release());
  };
  cobalt_options.service_path = "/svc/";
  cobalt_options.service_path.append(CobaltLogger::GetServiceName());
  return cobalt_options;
}
}  // namespace
}  // namespace internal

Collector::Collector(uint32_t project_id)
    : logger_(std::make_unique<internal::CobaltLogger>(internal::MakeCobaltOptions(project_id))) {
  flushing_.store(false);
}

Collector::Collector(std::unique_ptr<internal::Logger> logger) : logger_(std::move(logger)) {
  flushing_.store(false);
}

Collector::~Collector() {
  if (logger_ != nullptr) {
    Flush();
  }
}

bool Collector::Flush() {
  // If we are already flushing we just return and do nothing.
  // First come first serve.
  if (flushing_.exchange(true)) {
    return false;
  }

  bool all_flushed = true;
  for (internal::FlushInterface* flushable : flushables_) {
    if (!flushable->Flush(logger_.get())) {
      all_flushed = false;
      flushable->UndoFlush();
    }
  }

  // Once we are finished we allow flushing again.
  flushing_.store(false);

  return all_flushed;
}

void Collector::UnSubscribe(internal::FlushInterface* flushable) {
  ZX_ASSERT_MSG(flushables_.find(flushable) != flushables_.end(),
                "Unsubscribing a flushable that was not subscribed.");
  flushables_.erase(flushable);
}

void Collector::Subscribe(internal::FlushInterface* flushable) {
  ZX_ASSERT_MSG(flushables_.find(flushable) == flushables_.end(),
                "Subscribing same flushable multiple times.");
  flushables_.insert(flushable);
}

}  // namespace cobalt_client
