// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <utility>

#include <cobalt-client/cpp/collector_internal.h>
#include <cobalt-client/cpp/histogram_internal.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {
namespace internal {
namespace {

::llcpp::fuchsia::cobalt::CobaltEvent MetricIntoToCobaltEvent(const MetricOptions& metric_info) {
  llcpp::fuchsia::cobalt::CobaltEvent event;
  event.metric_id = metric_info.metric_id;
  event.component = fidl::unowned_str(metric_info.component);
  // Safe to do so, since the request is read only.
  event.event_codes = fidl::VectorView<uint32_t>(
      fidl::unowned_ptr(const_cast<uint32_t*>(metric_info.event_codes.data())),
      metric_info.metric_dimensions);
  return event;
}

}  // namespace

std::string_view CobaltLogger::GetServiceName() {
  return ::llcpp::fuchsia::cobalt::LoggerFactory::Name;
}

bool CobaltLogger::TryObtainLogger() {
  if (logger_.is_valid()) {
    return true;
  }

  zx::channel logger_factory_srv, logger_factory_client;

  if (zx::channel::create(0, &logger_factory_client, &logger_factory_srv) != ZX_OK) {
    return false;
  }

  if (options_.service_connect(options_.service_path.c_str(), std::move(logger_factory_srv)) !=
      ZX_OK) {
    return false;
  }

  zx::channel logger_svr;
  if (zx::channel::create(0, &logger_, &logger_svr) != ZX_OK) {
    return false;
  }

  // Obtain a logger service for the project ID.
  auto create_logger_result =
      ::llcpp::fuchsia::cobalt::LoggerFactory::Call::CreateLoggerFromProjectId(
          zx::unowned_channel(logger_factory_client), options_.project_id, std::move(logger_svr));
  return create_logger_result.status() == ZX_OK &&
         create_logger_result->status == ::llcpp::fuchsia::cobalt::Status::OK;
}

bool CobaltLogger::Log(const MetricOptions& metric_info, const HistogramBucket* buckets,
                       size_t bucket_count) {
  if (!TryObtainLogger()) {
    return false;
  }
  auto event = MetricIntoToCobaltEvent(metric_info);
  // Safe because is read only.
  auto int_histogram = fidl::VectorView<HistogramBucket>(
      fidl::unowned_ptr(const_cast<HistogramBucket*>(buckets)), bucket_count);
  event.payload.set_int_histogram(fidl::unowned_ptr(&int_histogram));

  auto log_result = llcpp::fuchsia::cobalt::Logger::Call::LogCobaltEvent(
      zx::unowned_channel(logger_), std::move(event));
  if (log_result.status() == ZX_ERR_PEER_CLOSED) {
    Reset();
  }
  return log_result.status() == ZX_OK && log_result->status == ::llcpp::fuchsia::cobalt::Status::OK;
}

bool CobaltLogger::Log(const MetricOptions& metric_info, RemoteCounter::Type count) {
  if (!TryObtainLogger()) {
    return false;
  }
  auto event = MetricIntoToCobaltEvent(metric_info);
  llcpp::fuchsia::cobalt::CountEvent event_count{.period_duration_micros = 0, .count = count};
  event.payload.set_event_count(fidl::unowned_ptr(&event_count));

  auto log_result = llcpp::fuchsia::cobalt::Logger::Call::LogCobaltEvent(
      zx::unowned_channel(logger_), std::move(event));
  if (log_result.status() == ZX_ERR_PEER_CLOSED) {
    Reset();
  }
  return log_result.status() == ZX_OK && log_result->status == ::llcpp::fuchsia::cobalt::Status::OK;
}

}  // namespace internal
}  // namespace cobalt_client
