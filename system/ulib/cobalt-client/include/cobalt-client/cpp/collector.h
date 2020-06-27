// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_COLLECTOR_H_
#define COBALT_CLIENT_CPP_COLLECTOR_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <string>

#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {

// This class acts as a peer for instantiating Histograms and Counters. All
// objects instantiated through this class act as a view, which means that
// their lifetime is coupled to this object's lifetime.
//
// The Sink provides an API for persisting the supported data types. This is
// exposed to simplify testing.
//
// This class is not moveable, copyable or assignable.
// This class is thread-compatible.
class Collector {
 public:
  // The ID used to register the project with cobalt. This will be used to route the
  // metrics to the right project.
  explicit Collector(uint32_t project_id);
  Collector(std::unique_ptr<internal::Logger> logger);
  Collector(const Collector&) = delete;
  Collector(Collector&&) = delete;
  Collector& operator=(const Collector&) = delete;
  Collector& operator=(Collector&&) = delete;
  ~Collector();

  // Allows classes implementing |internal::FlushInterface| to subscribe for Flush events.
  void Subscribe(internal::FlushInterface* flushable);

  // Allows classes implementing |internal::FlushInterface| to UnSubscribe for Flush events.
  void UnSubscribe(internal::FlushInterface* flushable);

  // Flushes the content of all flushable metrics into |logger_|. The |logger_| is
  // in charge of persisting the data.
  // Returns true when all flushable metrics flush successfully.
  bool Flush();

 private:
  std::set<internal::FlushInterface*> flushables_;

  std::unique_ptr<internal::Logger> logger_ = nullptr;
  std::atomic<bool> flushing_ = false;
};

}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_COLLECTOR_H_
