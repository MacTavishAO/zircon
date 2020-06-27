// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/metrics/composite_latency_event.h>

#include <lib/inspect/cpp/inspect.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>
#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <fs/metrics/cobalt_metrics.h>
#include <fs/metrics/events.h>
#include <fs/metrics/histograms.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {
namespace {

using internal::SelectHistogram;

constexpr std::string_view kComponentName = "test-metrics-fs";

class CompositeLatencyEventTest : public zxtest::Test {
 public:
  CompositeLatencyEventTest() : inspector_() {
    std::unique_ptr<cobalt_client::InMemoryLogger> logger =
        std::make_unique<cobalt_client::InMemoryLogger>();
    logger_ = logger.get();
    collector_ = std::make_unique<cobalt_client::Collector>(std::move(logger));
    metrics_ = std::make_unique<fs_metrics::VnodeMetrics>(collector_.get(), kComponentName);
    histograms_ = std::make_unique<Histograms>(&inspector_.GetRoot());
  }

 protected:
  inspect::Inspector inspector_;
  cobalt_client::InMemoryLogger* logger_;
  std::unique_ptr<cobalt_client::Collector> collector_;
  std::unique_ptr<fs_metrics::VnodeMetrics> metrics_;
  std::unique_ptr<Histograms> histograms_;
};

TEST_F(CompositeLatencyEventTest, SelectHistogramIsCorrect) {
  EXPECT_EQ(&metrics_->close, SelectHistogram(Event::kClose, metrics_.get()));
  EXPECT_EQ(&metrics_->read, SelectHistogram(Event::kRead, metrics_.get()));
  EXPECT_EQ(&metrics_->append, SelectHistogram(Event::kAppend, metrics_.get()));
  EXPECT_EQ(&metrics_->truncate, SelectHistogram(Event::kTruncate, metrics_.get()));
  EXPECT_EQ(&metrics_->set_attr, SelectHistogram(Event::kSetAttr, metrics_.get()));
  EXPECT_EQ(&metrics_->get_attr, SelectHistogram(Event::kGetAttr, metrics_.get()));
  EXPECT_EQ(&metrics_->read_dir, SelectHistogram(Event::kReadDir, metrics_.get()));
  EXPECT_EQ(&metrics_->sync, SelectHistogram(Event::kSync, metrics_.get()));
  EXPECT_EQ(&metrics_->look_up, SelectHistogram(Event::kLookUp, metrics_.get()));
  EXPECT_EQ(&metrics_->create, SelectHistogram(Event::kCreate, metrics_.get()));
  EXPECT_EQ(&metrics_->link, SelectHistogram(Event::kLink, metrics_.get()));
  EXPECT_EQ(&metrics_->unlink, SelectHistogram(Event::kUnlink, metrics_.get()));
  // This is not a Vnode event, and is not backed by an histogram, should return nullptr.
  EXPECT_EQ(nullptr, SelectHistogram(Event::kDataCorruption, metrics_.get()));
}

TEST_F(CompositeLatencyEventTest, SelectAppropiateHistogram) {
  constexpr uint32_t kCobaltOverflowHistogramBuckets = 2;
  for (auto event : kVnodeEvents) {
    CompositeLatencyEvent latency_event(event, histograms_.get(), metrics_.get());
    EXPECT_EQ(latency_event.mutable_latency_event()->event(), event);
    EXPECT_EQ(latency_event.mutable_histogram(), SelectHistogram(event, metrics_.get()));
    ASSERT_NOT_NULL(latency_event.mutable_histogram());
  }

  // Flush all logged metrics
  collector_->Flush();

  // Verify that cobalt persisted one observation for each metric.
  for (auto event : kVnodeEvents) {
    cobalt_client::MetricOptions options = {};
    options.metric_id = static_cast<uint32_t>(event);
    options.component = kComponentName;
    auto entry = logger_->histograms().find(options);
    EXPECT_NE(logger_->histograms().end(), entry);
    // There should be one event per bucket, since we made a one to one mapping for each event.
    EXPECT_EQ(fs_metrics::VnodeMetrics::kHistogramBuckets + kCobaltOverflowHistogramBuckets,
              entry->second.size());
    uint64_t total_observations = 0;
    for (const auto it : entry->second) {
      total_observations += it.second;
    }
    EXPECT_EQ(1, total_observations);
  }
}

}  // namespace
}  // namespace fs_metrics
