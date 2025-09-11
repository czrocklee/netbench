#pragma once

#include <boost/chrono/process_cpu_clocks.hpp>
#include <hdr/hdr_histogram.h>

#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <iostream>
#include <numeric>
#include <memory>

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

  struct sample
  {
    std::uint64_t send_ts;
    std::uint64_t recv_ts;
  };

  class metric_hud
  {
  public:
    metric_hud(std::chrono::seconds interval, std::move_only_function<metric()> action = nullptr);

    void tick();

    void collect(sample s, boost::chrono::process_cpu_clock::time_point now);

  private:
    using clock_type = boost::chrono::process_cpu_clock;

    clock_type::time_point start_time_;
    clock_type::time_point last_time_checked_;
    metric last_metric_;
    std::chrono::seconds const interval_;
    std::move_only_function<metric()> action_;
  };
}
