// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <ddktl/protocol/block.h>
#include <storage-metrics/block-metrics.h>
#include <storage-metrics/fs-metrics.h>
#include <storage-metrics/storage-metrics.h>
#include <zxtest/zxtest.h>

namespace storage_metrics {
namespace {

using storage_metrics::CallStat;

// Campare CallStat fields with the corresponding fields in
// ::llcpp::fuchsia::storage::metrics::CallStat structure
void ExpectCallStatMatchFidlStat(CallStat& cs,
                                 ::llcpp::fuchsia::storage::metrics::CallStat& cs_fidl) {
  EXPECT_EQ(cs.minimum_latency(true), cs_fidl.success.minimum_latency);
  EXPECT_EQ(cs.maximum_latency(true), cs_fidl.success.maximum_latency);
  EXPECT_EQ(cs.total_time_spent(true), cs_fidl.success.total_time_spent);
  EXPECT_EQ(cs.total_calls(true), cs_fidl.success.total_calls);
  EXPECT_EQ(cs.bytes_transferred(true), cs_fidl.success.bytes_transferred);

  EXPECT_EQ(cs.minimum_latency(false), cs_fidl.failure.minimum_latency);
  EXPECT_EQ(cs.maximum_latency(false), cs_fidl.failure.maximum_latency);
  EXPECT_EQ(cs.total_time_spent(false), cs_fidl.failure.total_time_spent);
  EXPECT_EQ(cs.total_calls(false), cs_fidl.failure.total_calls);
  EXPECT_EQ(cs.bytes_transferred(false), cs_fidl.failure.bytes_transferred);

  EXPECT_EQ(cs.minimum_latency(),
            std::min(cs_fidl.success.minimum_latency, cs_fidl.failure.minimum_latency));
  EXPECT_EQ(cs.maximum_latency(),
            std::max(cs_fidl.success.maximum_latency, cs_fidl.failure.maximum_latency));
  EXPECT_EQ(cs.total_time_spent(),
            (cs_fidl.success.total_time_spent + cs_fidl.failure.total_time_spent));
  EXPECT_EQ(cs.total_calls(), (cs_fidl.success.total_calls + cs_fidl.failure.total_calls));
  EXPECT_EQ(cs.bytes_transferred(),
            (cs_fidl.success.bytes_transferred + cs_fidl.failure.bytes_transferred));

  ::llcpp::fuchsia::storage::metrics::CallStat tmp;
  cs.CopyToFidl(&tmp);
}

// Deep campares two ::llcpp::fuchsia::storage::metrics::CallStatRaw structures. Can be
void ExpectFidlCallStatRawMatch(const ::llcpp::fuchsia::storage::metrics::CallStatRaw& lhs,
                                const ::llcpp::fuchsia::storage::metrics::CallStatRaw& rhs) {
  EXPECT_EQ(lhs.total_calls, rhs.total_calls);
  EXPECT_EQ(lhs.total_time_spent, rhs.total_time_spent);
  EXPECT_EQ(lhs.minimum_latency, rhs.minimum_latency);
  EXPECT_EQ(lhs.maximum_latency, rhs.maximum_latency);
  EXPECT_EQ(lhs.bytes_transferred, rhs.bytes_transferred);
}

// Compares two ::llcpp::fuchsia::storage::metrics::CallStat structures
void ExpectMetricsMatchCallStat(const ::llcpp::fuchsia::storage::metrics::CallStat& lhs,
                                const ::llcpp::fuchsia::storage::metrics::CallStat& rhs) {
  ExpectFidlCallStatRawMatch(lhs.success, rhs.success);
  ExpectFidlCallStatRawMatch(lhs.failure, rhs.failure);
}

// Compares all ::llcpp::fuchsia::storage::metrics::CallStat fields within |fidl_fs_metrics|,
// with |fidl_call_stat|
void CompareFidlFsStatAll(const ::llcpp::fuchsia::storage::metrics::FsMetrics& fidl_fs_metrics,
                          const ::llcpp::fuchsia::storage::metrics::CallStat& fidl_call_stat) {
  ExpectMetricsMatchCallStat(fidl_fs_metrics.create, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.read, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.write, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.truncate, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.unlink, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.rename, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.lookup, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_fs_metrics.open, fidl_call_stat);
}

// Updates all private CallStat fields of |metrics|
void UpdateAllFsMetricsRaw(storage_metrics::FsMetrics& metrics, bool success, zx_ticks_t delta,
                           uint64_t bytes_transferred) {
  metrics.UpdateCreateStat(success, delta, bytes_transferred);
  metrics.UpdateReadStat(success, delta, bytes_transferred);
  metrics.UpdateWriteStat(success, delta, bytes_transferred);
  metrics.UpdateTruncateStat(success, delta, bytes_transferred);
  metrics.UpdateUnlinkStat(success, delta, bytes_transferred);
  metrics.UpdateRenameStat(success, delta, bytes_transferred);
  metrics.UpdateLookupStat(success, delta, bytes_transferred);
  metrics.UpdateOpenStat(success, delta, bytes_transferred);
}

// Updates both success and failure stats with (|minimum_latency|, |bytes_transferred1|)
// (|maximum_latency|, |bytes_transferred2|) respectively.
void FsMetricsUpdate(storage_metrics::FsMetrics& metrics, zx_ticks_t minimum_latency,
                     zx_ticks_t maximum_latency, uint64_t bytes_transferred1,
                     uint64_t bytes_transferred2) {
  // Update successful minimum and maximum latencies
  UpdateAllFsMetricsRaw(metrics, true, minimum_latency, bytes_transferred1);
  UpdateAllFsMetricsRaw(metrics, true, maximum_latency, bytes_transferred2);
  UpdateAllFsMetricsRaw(metrics, false, minimum_latency, bytes_transferred1);
  UpdateAllFsMetricsRaw(metrics, false, maximum_latency, bytes_transferred2);
}

// Expects if |fidl_fs_mterics| is properly initialized.
void ExpectFsMetricsInitialState(
    const ::llcpp::fuchsia::storage::metrics::FsMetrics& fidl_fs_metrics) {
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_call_stat = {};
  fidl_call_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  fidl_call_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
}

// Updates all private CallStat fields of |metrics|
void UpdateAllBlockDeviceMetricsRaw(storage_metrics::BlockDeviceMetrics& metrics, bool success,
                                    zx_ticks_t delta, uint64_t bytes_transferred) {
  metrics.UpdateReadStat(success, delta, bytes_transferred);
  metrics.UpdateWriteStat(success, delta, bytes_transferred);
  metrics.UpdateTrimStat(success, delta, bytes_transferred);
  metrics.UpdateFlushStat(success, delta, bytes_transferred);
  metrics.UpdateBarrierBeforeStat(success, delta, bytes_transferred);
  metrics.UpdateBarrierAfterStat(success, delta, bytes_transferred);
}

// Updates both success and failure stats with (|minimum_latency|, |bytes_transferred1|)
// (|maximum_latency|, |bytes_transferred2|) respectively.
void BlockDeviceMetricsUpdate(storage_metrics::BlockDeviceMetrics& metrics,
                              zx_ticks_t minimum_latency, zx_ticks_t maximum_latency,
                              uint64_t bytes_transferred1, uint64_t bytes_transferred2) {
  // Update successful minimum and maximum latencies
  UpdateAllBlockDeviceMetricsRaw(metrics, true, minimum_latency, bytes_transferred1);
  UpdateAllBlockDeviceMetricsRaw(metrics, true, maximum_latency, bytes_transferred2);
  UpdateAllBlockDeviceMetricsRaw(metrics, false, minimum_latency, bytes_transferred1);
  UpdateAllBlockDeviceMetricsRaw(metrics, false, maximum_latency, bytes_transferred2);
}

// Compares all ::llcpp::fuchsia::storage::metrics::CallStat fields within
// |fidl_block_device_metrics|, with |fidl_call_stat|
void CompareFidlBlockDeviceStatAll(
    const ::llcpp::fuchsia::hardware::block::BlockStats& fidl_block_device_metrics,
    const ::llcpp::fuchsia::storage::metrics::CallStat& fidl_call_stat) {
  ExpectMetricsMatchCallStat(fidl_block_device_metrics.read, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_block_device_metrics.write, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_block_device_metrics.flush, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_block_device_metrics.trim, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_block_device_metrics.barrier_before, fidl_call_stat);
  ExpectMetricsMatchCallStat(fidl_block_device_metrics.barrier_after, fidl_call_stat);
}

// Expects if |fidl_fs_metrics| is properly initialized.
void ExpectBlockDeviceMetricsInitialState(
    const ::llcpp::fuchsia::hardware::block::BlockStats& fidl_block_device_metrics) {
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_call_stat = {};
  fidl_call_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  fidl_call_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  CompareFidlBlockDeviceStatAll(fidl_block_device_metrics, fidl_call_stat);
}

TEST(RawCallStatEqual, Same) {
  CallStatRawFidl a = {}, b = {};
  ASSERT_TRUE(RawCallStatEqual(a, b));
}

TEST(RawCallStatEqual, LargerTotalCalls) {
  CallStatRawFidl a = {}, b = {};
  a.total_calls++;
  ASSERT_FALSE(RawCallStatEqual(a, b));
}

TEST(RawCallStatEqual, LargerBytesTransferred) {
  CallStatRawFidl a = {}, b = {};
  a.bytes_transferred++;
  ASSERT_FALSE(RawCallStatEqual(a, b));
}

TEST(CallStatEqual, Same) {
  CallStatFidl a = {}, b = {};
  ASSERT_TRUE(CallStatEqual(a, b));
}

TEST(CallStatEqual, LargerTotalCalls) {
  CallStatFidl a = {}, b = {};
  a.success.total_calls++;
  ASSERT_FALSE(CallStatEqual(a, b));
}

TEST(CallStatEqual, LargerBytesTransferred) {
  CallStatFidl a = {}, b = {};
  a.failure.bytes_transferred++;
  ASSERT_FALSE(CallStatEqual(a, b));
}

TEST(BlockStatEqual, Same) {
  BlockStatFidl a = {}, b = {};
  ASSERT_TRUE(BlockStatEqual(a, b));
}

TEST(BlockStatEqual, LargerReadTotalCalls) {
  BlockStatFidl a = {}, b = {};
  a.read.success.total_calls++;
  ASSERT_FALSE(BlockStatEqual(a, b));
}

TEST(BlockStatEqual, LargerWriteBytesTransferred) {
  BlockStatFidl a = {}, b = {};
  a.write.failure.bytes_transferred++;
  ASSERT_FALSE(BlockStatEqual(a, b));
}

TEST(CallStatTest, UpdateSuccess) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  // Copy initial state
  cs.CopyToFidl(&fidl_stat);

  cs.UpdateCallStat(true, 10, 100);
  fidl_stat.success.total_calls++;
  fidl_stat.success.total_time_spent += 10;
  fidl_stat.success.minimum_latency = 10;
  fidl_stat.success.maximum_latency = 10;
  fidl_stat.success.bytes_transferred += 100;
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateFailure) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  // Copy initial state
  cs.CopyToFidl(&fidl_stat);

  // No change in success stats but everything else changes
  cs.UpdateCallStat(false, 10, 100);
  fidl_stat.failure.total_calls++;
  fidl_stat.failure.total_time_spent += 10;
  fidl_stat.failure.minimum_latency = 10;
  fidl_stat.failure.maximum_latency = 10;
  fidl_stat.failure.bytes_transferred += 100;
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateBytesTransferred) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  // Copy initial state
  cs.CopyToFidl(&fidl_stat);

  // No change in min/max latencies or failure but everything else changes
  cs.UpdateCallStat(true, 10, 100);
  fidl_stat.success.total_calls++;
  fidl_stat.success.total_time_spent += 10;
  fidl_stat.success.minimum_latency = 10;
  fidl_stat.success.maximum_latency = 10;
  fidl_stat.success.bytes_transferred += 100;
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateMinimumLatency) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  // Copy initial state
  cs.CopyToFidl(&fidl_stat);

  // Expect min latency to change and bytes transferred unchanged
  cs.UpdateCallStat(true, 9, 0);
  cs.UpdateCallStat(true, 7, 0);
  fidl_stat.success.total_calls += 2;
  fidl_stat.success.total_time_spent += (9 + 7);
  fidl_stat.success.minimum_latency = 7;
  fidl_stat.success.maximum_latency = 9;
  fidl_stat.success.bytes_transferred += 0;
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateFailedMaximumLatency) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  // Copy initial state
  cs.CopyToFidl(&fidl_stat);

  // Expect max latency and failed count to change
  cs.UpdateCallStat(false, 20, 100);
  cs.UpdateCallStat(false, 30, 100);
  fidl_stat.failure.total_calls += 2;
  fidl_stat.failure.total_time_spent += (20 + 30);
  fidl_stat.failure.minimum_latency = 20;
  fidl_stat.failure.maximum_latency = 30;
  fidl_stat.failure.bytes_transferred += (100 + 100);
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateTimeSpent) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  // Copy initial state
  cs.CopyToFidl(&fidl_stat);

  // Expect only time spent and total calls to change
  cs.UpdateCallStat(true, 20, 0);
  cs.UpdateCallStat(true, 20, 0);
  fidl_stat.success.total_calls += 2;
  fidl_stat.success.minimum_latency = 20;
  fidl_stat.success.maximum_latency = 20;
  fidl_stat.success.total_time_spent += (20 + 20);
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, Reset) {
  storage_metrics::CallStat cs = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_stat;

  cs.UpdateCallStat(true, 20, 100);
  cs.UpdateCallStat(false, 20, 100);

  // Everything should be cleared
  cs.Reset();
  fidl_stat = {};
  fidl_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  fidl_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, TestCopyToFidl) {
  ::llcpp::fuchsia::storage::metrics::CallStat f = {};
  storage_metrics::CallStat cs;

  // Set max latency
  cs.UpdateCallStat(true, 20, 100);

  // Set min latency and fail count
  cs.UpdateCallStat(true, 10, 20);
  cs.CopyToFidl(&f);

  ExpectCallStatMatchFidlStat(cs, f);
}

TEST(CallStatTest, TestCopyFromFidl) {
  ::llcpp::fuchsia::storage::metrics::CallStat f;
  storage_metrics::CallStat cs;

  f.success.total_calls = 3;
  f.success.minimum_latency = 4;
  f.success.maximum_latency = 15;
  f.success.total_time_spent = 19;
  f.success.bytes_transferred = 92;
  f.failure.total_calls = 3;
  f.failure.minimum_latency = 4;
  f.failure.maximum_latency = 15;
  f.failure.total_time_spent = 19;
  f.failure.bytes_transferred = 92;
  cs.CopyFromFidl(&f);

  ExpectCallStatMatchFidlStat(cs, f);
}

// Tests enable/disable functionality
TEST(MetricsTest, SetEnable) {
  storage_metrics::Metrics metrics;

  EXPECT_TRUE(metrics.Enabled());
  metrics.SetEnable(false);
  EXPECT_FALSE(metrics.Enabled());
  metrics.SetEnable(true);
  EXPECT_TRUE(metrics.Enabled());
}

// Test initial state of FsMetrics
TEST(FsMetricsTest, DefaultValues) {
  storage_metrics::FsMetrics metrics;
  ::llcpp::fuchsia::storage::metrics::FsMetrics fidl_fs_metrics;
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_call_stat = {};
  fidl_call_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
  fidl_call_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;

  EXPECT_TRUE(metrics.Enabled());

  metrics.CopyToFidl(&fidl_fs_metrics);
  CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
}

// Tests no-updates are made when metrics is disabled
TEST(FsMetricsTest, DisabledMetricsIgnoreUpdates) {
  storage_metrics::FsMetrics metrics;
  ::llcpp::fuchsia::storage::metrics::FsMetrics fidl_fs_metrics;

  EXPECT_TRUE(metrics.Enabled());

  metrics.SetEnable(false);
  EXPECT_FALSE(metrics.Enabled());

  // When not enabled, this should not update anything
  FsMetricsUpdate(metrics, 10, 100, 100, 800);

  metrics.CopyToFidl(&fidl_fs_metrics);
  ExpectFsMetricsInitialState(fidl_fs_metrics);
}

// Tests updates to fs metrics
TEST(FsMetricsTest, EnabledMetricsCollectOnUpdate) {
  storage_metrics::FsMetrics metrics;
  ::llcpp::fuchsia::storage::metrics::FsMetrics fidl_fs_metrics;
  ::llcpp::fuchsia::storage::metrics::CallStatRaw fidl_call_stat_raw = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_call_stat;
  EXPECT_TRUE(metrics.Enabled());

  zx_ticks_t minimum_latency = 10;
  zx_ticks_t maximum_latency = 100;
  uint64_t bytes_transferred1 = 330;
  uint64_t bytes_transferred2 = 440;

  FsMetricsUpdate(metrics, minimum_latency, maximum_latency, bytes_transferred1,
                  bytes_transferred2);

  metrics.CopyToFidl(&fidl_fs_metrics);
  fidl_call_stat_raw.minimum_latency = minimum_latency;
  fidl_call_stat_raw.maximum_latency = maximum_latency;
  fidl_call_stat_raw.total_time_spent = minimum_latency + maximum_latency;
  fidl_call_stat_raw.total_calls = 2;
  fidl_call_stat_raw.bytes_transferred = bytes_transferred1 + bytes_transferred2;
  fidl_call_stat.success = fidl_call_stat_raw;
  fidl_call_stat.failure = fidl_call_stat_raw;

  CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);

  // Disable enable should not change the metrics
  metrics.SetEnable(false);
  metrics.CopyToFidl(&fidl_fs_metrics);
  CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
  metrics.SetEnable(true);
  metrics.CopyToFidl(&fidl_fs_metrics);
  CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
}

// Test initial state of BlockDeviceMetrics
TEST(BlockDeviceMetricsTest, DefaultValues) {
  storage_metrics::BlockDeviceMetrics metrics;
  ::llcpp::fuchsia::hardware::block::BlockStats fidl_block_metrics;

  EXPECT_TRUE(metrics.Enabled());

  metrics.CopyToFidl(&fidl_block_metrics);
  ExpectBlockDeviceMetricsInitialState(fidl_block_metrics);
}

// Tests no-updates are made when metrics is disabled
TEST(BlockDeviceMetricsTest, DisabledMetricsIgnoreUpdates) {
  storage_metrics::BlockDeviceMetrics metrics;
  ::llcpp::fuchsia::hardware::block::BlockStats fidl_block_metrics;

  ASSERT_TRUE(metrics.Enabled());
  metrics.CopyToFidl(&fidl_block_metrics);
  ExpectBlockDeviceMetricsInitialState(fidl_block_metrics);

  metrics.SetEnable(false);
  EXPECT_FALSE(metrics.Enabled());

  // When not enabled, this should not update anything
  BlockDeviceMetricsUpdate(metrics, 10, 100, 100, 800);

  metrics.CopyToFidl(&fidl_block_metrics);
  ExpectBlockDeviceMetricsInitialState(fidl_block_metrics);
}

// Tests updates to block device metrics
TEST(BlockDeviceMetricsTest, EnabledMetricsCollectOnUpdate) {
  storage_metrics::BlockDeviceMetrics metrics;
  ::llcpp::fuchsia::hardware::block::BlockStats fidl_block_metrics;
  ::llcpp::fuchsia::storage::metrics::CallStatRaw fidl_call_stat_raw = {};
  ::llcpp::fuchsia::storage::metrics::CallStat fidl_call_stat;
  ASSERT_TRUE(metrics.Enabled());

  zx_ticks_t minimum_latency = 10;
  zx_ticks_t maximum_latency = 100;
  uint64_t bytes_transferred1 = 330;
  uint64_t bytes_transferred2 = 440;

  BlockDeviceMetricsUpdate(metrics, minimum_latency, maximum_latency, bytes_transferred1,
                           bytes_transferred2);

  metrics.CopyToFidl(&fidl_block_metrics);
  fidl_call_stat_raw.minimum_latency = minimum_latency;
  fidl_call_stat_raw.maximum_latency = maximum_latency;
  fidl_call_stat_raw.total_time_spent = minimum_latency + maximum_latency;
  fidl_call_stat_raw.total_calls = 2;
  fidl_call_stat_raw.bytes_transferred = bytes_transferred1 + bytes_transferred2;
  fidl_call_stat.success = fidl_call_stat_raw;
  fidl_call_stat.failure = fidl_call_stat_raw;

  CompareFidlBlockDeviceStatAll(fidl_block_metrics, fidl_call_stat);

  // Disable enable should not change the metrics
  metrics.SetEnable(false);
  metrics.CopyToFidl(&fidl_block_metrics);
  CompareFidlBlockDeviceStatAll(fidl_block_metrics, fidl_call_stat);
  metrics.SetEnable(true);
  metrics.CopyToFidl(&fidl_block_metrics);
  CompareFidlBlockDeviceStatAll(fidl_block_metrics, fidl_call_stat);
}

TEST(BlockDeviceMetricsTest, UpdateWriteStats) {
  storage_metrics::BlockDeviceMetrics metrics;
  fuchsia_hardware_block_BlockStats fidl_block_metrics;

  metrics.UpdateStats(true, zx::ticks(0), BLOCK_OP_WRITE, 100);
  metrics.UpdateStats(false, zx::ticks(0), BLOCK_OP_WRITE, 10);
  metrics.CopyToFidl(&fidl_block_metrics);

  ASSERT_EQ(1, fidl_block_metrics.write.success.total_calls);
  ASSERT_EQ(100, fidl_block_metrics.write.success.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.write.success.total_time_spent);
  ASSERT_EQ(1, fidl_block_metrics.write.failure.total_calls);
  ASSERT_EQ(10, fidl_block_metrics.write.failure.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.write.failure.total_time_spent);
}

TEST(BlockDeviceMetricsTest, UpdateReadStats) {
  storage_metrics::BlockDeviceMetrics metrics;
  fuchsia_hardware_block_BlockStats fidl_block_metrics;

  metrics.UpdateStats(true, zx::ticks(0), BLOCK_OP_READ, 100);
  metrics.UpdateStats(false, zx::ticks(0), BLOCK_OP_READ, 10);
  metrics.CopyToFidl(&fidl_block_metrics);

  ASSERT_EQ(1, fidl_block_metrics.read.success.total_calls);
  ASSERT_EQ(100, fidl_block_metrics.read.success.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.read.success.total_time_spent);
  ASSERT_EQ(1, fidl_block_metrics.read.failure.total_calls);
  ASSERT_EQ(10, fidl_block_metrics.read.failure.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.read.failure.total_time_spent);
}

TEST(BlockDeviceMetricsTest, UpdateFlushStats) {
  storage_metrics::BlockDeviceMetrics metrics;
  fuchsia_hardware_block_BlockStats fidl_block_metrics;

  metrics.UpdateStats(true, zx::ticks(0), BLOCK_OP_FLUSH, 100);
  metrics.UpdateStats(false, zx::ticks(0), BLOCK_OP_FLUSH, 10);
  metrics.CopyToFidl(&fidl_block_metrics);

  ASSERT_EQ(1, fidl_block_metrics.flush.success.total_calls);
  ASSERT_EQ(100, fidl_block_metrics.flush.success.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.flush.success.total_time_spent);
  ASSERT_EQ(1, fidl_block_metrics.flush.failure.total_calls);
  ASSERT_EQ(10, fidl_block_metrics.flush.failure.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.flush.failure.total_time_spent);
}

TEST(BlockDeviceMetricsTest, UpdateTrimStats) {
  storage_metrics::BlockDeviceMetrics metrics;
  fuchsia_hardware_block_BlockStats fidl_block_metrics;

  metrics.UpdateStats(true, zx::ticks(0), BLOCK_OP_TRIM, 100);
  metrics.UpdateStats(false, zx::ticks(0), BLOCK_OP_TRIM, 10);
  metrics.CopyToFidl(&fidl_block_metrics);

  ASSERT_EQ(1, fidl_block_metrics.trim.success.total_calls);
  ASSERT_EQ(100, fidl_block_metrics.trim.success.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.trim.success.total_time_spent);
  ASSERT_EQ(1, fidl_block_metrics.trim.failure.total_calls);
  ASSERT_EQ(10, fidl_block_metrics.trim.failure.bytes_transferred);
  ASSERT_LT(0, fidl_block_metrics.trim.failure.total_time_spent);
}

TEST(BlockDeviceMetricsTest, UpdateBarrierStats) {
  storage_metrics::BlockDeviceMetrics metrics;
  fuchsia_hardware_block_BlockStats fidl_block_metrics;

  metrics.UpdateStats(true, zx::ticks(0), BLOCK_OP_READ | BLOCK_FL_BARRIER_BEFORE, 100);
  metrics.UpdateStats(false, zx::ticks(0), BLOCK_OP_WRITE | BLOCK_FL_BARRIER_AFTER, 10);
  metrics.UpdateStats(true, zx::ticks(0),
                      BLOCK_OP_TRIM | BLOCK_FL_BARRIER_AFTER | BLOCK_FL_BARRIER_BEFORE, 20);
  metrics.CopyToFidl(&fidl_block_metrics);

  ASSERT_EQ(1, fidl_block_metrics.read.success.total_calls);
  ASSERT_EQ(1, fidl_block_metrics.write.failure.total_calls);
  ASSERT_EQ(1, fidl_block_metrics.trim.success.total_calls);
  ASSERT_EQ(2, fidl_block_metrics.barrier_before.success.total_calls);
  ASSERT_EQ(1, fidl_block_metrics.barrier_after.failure.total_calls);
  ASSERT_EQ(1, fidl_block_metrics.barrier_after.success.total_calls);

  ASSERT_EQ(100, fidl_block_metrics.read.success.bytes_transferred);
  ASSERT_EQ(10, fidl_block_metrics.write.failure.bytes_transferred);
  ASSERT_EQ(20, fidl_block_metrics.trim.success.bytes_transferred);
  ASSERT_EQ(120, fidl_block_metrics.barrier_before.success.bytes_transferred);
  ASSERT_EQ(10, fidl_block_metrics.barrier_after.failure.bytes_transferred);
  ASSERT_EQ(20, fidl_block_metrics.barrier_after.success.bytes_transferred);

  ASSERT_LT(0, fidl_block_metrics.barrier_before.success.total_time_spent);
  ASSERT_EQ(0, fidl_block_metrics.barrier_before.failure.total_time_spent);
  ASSERT_LT(0, fidl_block_metrics.barrier_after.success.total_time_spent);
  ASSERT_LT(0, fidl_block_metrics.barrier_after.failure.total_time_spent);
}

}  // namespace
}  // namespace storage_metrics
