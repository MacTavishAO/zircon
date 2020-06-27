// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_METRICS_HISTOGRAMS_H_
#define FS_METRICS_HISTOGRAMS_H_

#include <limits>
#include <vector>

#include <fs/metrics/events.h>
#include <lib/fzl/time.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/time.h>

namespace fs_metrics {

// Properties of logged events, which are used internally to find the correct histogram.
struct EventOptions {
  // Matches the block range of an event.
  int64_t block_count = 0;
  // Used match the depth range of an event.
  int64_t node_depth = 0;
  // Used to match the node degree range of an event.
  int64_t node_degree = 0;
  // Used to mark an event as buffered or cache-hit depending on the context.
  bool buffered = false;
  // Used to mark an event as successfully completed.
  bool success = false;
};

namespace internal {
// RAII wrapper for keeping track of duration, by calling RecordFn. It's templated on the Clock
// and histogram for ease of testing.
template <typename T, typename V>
class LatencyEventInternal {
 public:
  using HistogramCollection = T;
  using Clock = V;

  LatencyEventInternal() = delete;
  explicit LatencyEventInternal(HistogramCollection* histograms, Event event)
      : event_(event), histograms_(histograms) {
    Reset();
  }

  LatencyEventInternal(const LatencyEventInternal&) = delete;
  LatencyEventInternal(LatencyEventInternal&& other) {
    options_ = other.options_;
    event_ = other.event_;
    histograms_ = other.histograms_;
    start_ = other.start_;
    other.Cancel();
  }
  LatencyEventInternal& operator=(const LatencyEventInternal&) = delete;
  LatencyEventInternal& operator=(LatencyEventInternal&&) = delete;
  ~LatencyEventInternal() { Record(); }

  // Explicitly record the latency event, since creation or last call to |LatencyEvent::Reset|
  // until now.
  void Record() {
    if (start_.get() == 0) {
      return;
    }
    histograms_->Record(histograms_->GetHistogramId(event_, options_),
                        fzl::TicksToNs(Clock::now() - start_));
    Cancel();
  }

  // Resets the start time from the observation, and the event starts tracking again.
  // |options| remain the same.
  void Reset() { start_ = Clock::now(); }

  // Prevents this observation from being recorded.
  void Cancel() { start_ = zx::ticks(0); }

  // Updating the options may change which histogram records this observation.
  EventOptions* mutable_options() { return &options_; }

  // Returns the point at which the event started recording latency.
  zx::ticks start() const { return start_; }

  // Returns the event type to be recorded using this |LatencyEvent|.
  Event event() const { return event_; }

 private:
  EventOptions options_ = {};
  // Records an observation in histograms when LatencyEvent is destroyed or explictly
  // requested to record.
  Event event_;
  HistogramCollection* histograms_ = nullptr;
  zx::ticks start_ = zx::ticks(0);
};
}  // namespace internal

// Forward declaration.
class Histograms;

// Alias for exposing the LatencyEvent actually used.
using LatencyEvent = internal::LatencyEventInternal<Histograms, zx::ticks>;

// This class provides a unified view over common metrics collected for file systems.
class Histograms {
 public:
  static constexpr char kHistComponent[] = "histograms";

  // Returns the number of bytes rounded up to the nearest page required to store
  // all the hitograms in the collection.
  static uint64_t Size();

  Histograms() = delete;
  explicit Histograms(inspect::Node* root);
  Histograms(const Histograms&) = delete;
  Histograms(Histograms&&) = delete;
  Histograms& operator=(const Histograms&) = delete;
  Histograms& operator=(Histograms&&) = delete;
  ~Histograms() = default;

  // Returns a LatencyEvent that will record a latency event for |event| on destruction unless
  // it is cancelled. |LatencyEvent::mutable_options| allows adjusting the event options.
  LatencyEvent NewLatencyEvent(Event event);

  // Returns a unique Id for a given event and option set. Depending on the event,
  // multiple option configurations may be mapped to the same Id. The histogram ids are in the
  // range [0, HistogramCount).
  uint64_t GetHistogramId(Event event, const EventOptions& options) const;

  // Returns the number of different histograms tracking this event.
  uint64_t GetHistogramCount(Event event);

  // Returns the number of histograms in this collection.
  uint64_t GetHistogramCount() const { return histograms_.size(); }

  // Records |latency| into the histogram mapped to |histogram_id|.
  void Record(uint64_t histogram_id, zx::duration latency);

 protected:
  // Nodes of the inspect tree created for the histogram hierarchy.
  std::vector<inspect::Node> nodes_;

  // Collection of histograms created for each collected metric.
  std::vector<inspect::ExponentialUintHistogram> histograms_;
};

}  // namespace fs_metrics

#endif  // FS_METRICS_HISTOGRAMS_H_
