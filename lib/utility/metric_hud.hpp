#pragma once

#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <iostream>
#include <numeric>
#include <memory>

#include <hdr/hdr_histogram.h>

namespace utility
{
  struct metric
  {
    void init_histogram();
    void add(metric const& other);
    void update_latency_histogram(std::uint64_t value) { ::hdr_record_value(latency_hist.get(), value); }

    std::uint64_t ops = 0;
    std::uint64_t msgs = 0;
    std::uint64_t bytes = 0;
    std::unique_ptr<::hdr_histogram, decltype(&::hdr_close)> latency_hist{nullptr, ::hdr_close};
  };

  class metric_hud
  {
  public:
    metric_hud(std::chrono::seconds interval, std::move_only_function<metric()> action)
      : interval_(interval), action_(std::move(action))
    {
    }

    void tick();

  private:
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_time_checked_;
    metric last_metric_;
    std::chrono::seconds const interval_;
    std::move_only_function<metric()> action_;
  };
}
