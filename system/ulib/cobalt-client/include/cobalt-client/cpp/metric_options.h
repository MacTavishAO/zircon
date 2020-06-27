// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_METRIC_OPTIONS_H_
#define COBALT_CLIENT_CPP_METRIC_OPTIONS_H_

#include <array>
#include <cstdint>
#include <string>

namespace cobalt_client {

struct MetricOptions {
  static constexpr uint64_t kMaxEventCodes = 5;

  // Allows using a |MetricOptions| as key in ordered containers.
  struct LessThan {
    bool operator()(const MetricOptions& lhs, const MetricOptions& rhs) const {
      if (lhs.component < rhs.component) {
        return true;
      } else if (lhs.component > rhs.component) {
        return false;
      }
      if (lhs.metric_id < rhs.metric_id) {
        return true;
      } else if (lhs.metric_id > rhs.metric_id) {
        return false;
      }
      return lhs.event_codes < rhs.event_codes;
    }
  };

  // Allows comparing two |MetricOptions|, which is a shortcut for checking if
  // all fields are equal.
  bool operator==(const MetricOptions& rhs) const {
    return rhs.metric_id == metric_id && rhs.event_codes == event_codes &&
           rhs.component == component;
  }
  bool operator!=(const MetricOptions& rhs) const { return !(*this == rhs); }

  // Provides refined metric collection for remote metrics.
  // Warning: |component| is not yet supported in the backend, so it will be ignored.
  std::string component = {};

  // Used by remote metrics to match with the respective unique id for the projects defined
  // metrics in the backend.
  uint32_t metric_id = {};

  // Number of dimensions defined in the cobalt metric definition.
  uint32_t metric_dimensions = 0;

  // This is the equivalent of the event enums defined in the cobalt configuration, because of
  // this order matters.
  //
  // E.g. Metric{id:1, event_codes:{0,0,0,0,1}}
  //      Metric{id:1, event_codes:{0,0,0,0,2}}
  // Can be seen independently in the cobalt backend, or aggregated together.
  // The sent data will be limited by |max_event_codes|.
  std::array<uint32_t, kMaxEventCodes> event_codes = {0, 0, 0, 0, 0};
};

// Describes an histogram, and provides data for mapping a value to a given bucket.
// Every histogram contains two additional buckets, one at index 0 and bucket_count + 1.
// These buckets are used to store underflow and overflow respectively.
//
// buckets = [-inf, min_value) ...... [max_value, +inf)
//
// Parameters are calculated by the factory methods based on the input parameters,
// so that expectations are met.
//
// If using cobalt to flush your observations to the backend, this options should match
// your metric definitions for correct behavior. Mismatch with the respective metric definition
// will not allow proper collection and aggregation of metrics in the backend.
struct HistogramOptions : public MetricOptions {
  enum class Type {
    // Each bucket is described in the following form:
    // range(i) =  [ b * i + c, b * {i +1} + c)
    // i = (val - c) / b
    kLinear,
    // Each bucket is described in the following form:
    // range(i) =  [ b * a^i + c, b * a^{i+1} + c)
    // The cost of this type is O(1), because:
    // i = floor(log (val - c)  - log b)/log a
    kExponential,
  };

  // Returns HistogramOptions that:
  //   * Exponential bucket range with base 2 => lower_bound[i] = a*2^i-1 - 1
  //   * Has underflow bucket from (-inf, 0)
  //   * The first bucket contains [0, 1)
  //   * a is an integer greater or equal to 1.
  //   * if |max| % (2^|bucket_count| - 1):
  //      - Is not 0, then |max| is contained in the last bucket.
  //      - Is 0, then |max| is the lower bound of the overflow bucket.
  //
  //   For example:
  //       - With bucket_count 12 and max 40950, we get scalar 10, base 2,
  //         and offset -10.
  //       - With bucket_count 12 and max 40960, we get scalar 11, base 2,
  //         and offset -11.
  static HistogramOptions Exponential(uint32_t bucket_count, int64_t max);

  // Returns HistogramOptions that:
  //   * Exponential bucket range with base 2 => lower_bound[i] = a*2^i-1 - 1
  //   * Has underflow bucket from (-inf, min)
  //   * The first bucket contains [min, min+1)
  //   * a is an integer greater or equal to 1.
  //   * if |max - min| % (2^|bucket_count| - 1):
  //      - Is not, then |max| is contained in the last bucket.
  //      - Is 0, then |max| is the lower bound of the overflow bucket.
  static HistogramOptions Exponential(uint32_t bucket_count, int64_t min, int64_t max);

  // Returns HistogramOptions that:
  //   * Has an extra underflow bucket.
  //   * Has an extra overflow bucket.
  //   * For every bucket from i = 1 to |bucket_count| has a lower bound defined by:
  //       scalar * (base^(i-1) - 1) + min
  static HistogramOptions CustomizedExponential(uint32_t bucket_count, uint32_t base,
                                                uint32_t scalar, int64_t min);

  // Returns HistogramOptions that:
  //   * Linear bucket range with fixed step size ceil(|max|/|bucket_count|).
  //   * Has underflow bucket from (-inf, 0)
  //   * The first bucket contains [0, step_size)
  //   * |max| is contained in the last bucket if its not a multiple of |bucket_count|.
  static HistogramOptions Linear(uint32_t bucket_count, int64_t max);

  // Returns HistogramOptions that:
  //   * Linear bucket range with fixed step size ceil(|max|/|bucket_count|).
  //   * Has underflow bucket from (-inf, 0)
  //   * The first bucket contains [0, step_size)
  //   * |max| is contained in the last bucket if its not a multiple of |bucket_count|.
  static HistogramOptions Linear(uint32_t bucket_count, int64_t min, int64_t max);

  // Returns HistogramOptions that:
  //   * Has an extra underflow bucket.
  //   * Has an extra overflow bucket.
  //   * For every bucket from i = 1 to |bucket_count| has a lower bound defined by:
  //       min + step_size * (i-1)
  static HistogramOptions CustomizedLinear(uint32_t bucket_count, uint32_t step_size, int64_t min);

  HistogramOptions() = default;
  HistogramOptions(const HistogramOptions&);
  HistogramOptions& operator=(const HistogramOptions&);

  // Sanity check.
  bool IsValid() const;

  // This parameters should not be set manually.

  // Function used for mapping a value to a given bucket.
  uint32_t (*map_fn)(double, uint32_t, const HistogramOptions&) = nullptr;

  // Function used for mapping a bucket to its lowerbound.
  double (*reverse_map_fn)(uint32_t, uint32_t, const HistogramOptions&) = nullptr;

  // Base to describe the width of each step, in |kExponentialWidth|.
  double base = 1;

  // Scalar used by the type. This scales the width of each step.
  double scalar = 1;

  // This matchest offset', which is calculated depending on the histogram type.
  double offset = 0;

  // Bounds for the histogram.
  double max_value = 0;

  // Type of the histogram to be constructed.
  Type type;
};

}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_METRIC_OPTIONS_H_
