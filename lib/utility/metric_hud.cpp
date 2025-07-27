#include "metric_hud.hpp"
#include <iostream>
#include <string>
#include <cstdint>
#include <iomanip> // For std::fixed, std::setprecision
#include <sstream> // For std::stringstream

namespace utility
{
  void metric::init_histogram()
  {
    latency_hist = decltype(latency_hist){
      [] {
        hdr_histogram* h;
        ::hdr_init(1, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}).count(), 3, &h);
        return h;
      }(),
      ::hdr_close};
  }

  void metric::add(metric const& other)
  {
    ops += other.ops;
    msgs += other.msgs;
    bytes += other.bytes;
    ::hdr_add(latency_hist.get(), other.latency_hist.get());
  }

  namespace
  {
    std::string pretty_print(double value, int precision = 1)
    {
      if (std::abs(value) < 1000.0)
      {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(precision) << value;
        return ss.str();
      }

      char const* suffixes[] = {"", "k", "M", "G", "T"};
      int suffix_index = 0;
      double temp_value = value;

      while (std::abs(temp_value) >= 1000.0 && suffix_index < 4)
      {
        temp_value /= 1000.0;
        ++suffix_index;
      }

      std::stringstream ss;
      ss << std::fixed << std::setprecision(precision) << temp_value << suffixes[suffix_index];
      return ss.str();
    }
  }

  metric_hud::metric_hud(std::chrono::seconds interval, std::move_only_function<metric()> action)
    : interval_(interval), action_(std::move(action))
  {
    if (action == nullptr) { last_metric_.init_histogram(); }
  }

  void metric_hud::tick()
  {
    auto const now = std::chrono::steady_clock::now();
    constexpr std::string_view format_line =
      "{:>8} / {:8}  {:>8} / {:8}  {:>8} / {:8}  {:>8} / {:8}  {:>8}  {:>8}  {:>8}";

    // Only update if the interval has passed, or if it's the very first tick
    if (start_time_ == std::chrono::steady_clock::time_point{} || (now - last_time_checked_ >= interval_))
    {
      // Initialize start_time_ on the first actual tick
      if (start_time_ == std::chrono::steady_clock::time_point{})
      {
        start_time_ = now;
        std::cout << std::format(
                       format_line,
                       "ops",
                       "(all)",
                       "msgs",
                       "(all)",
                       "bytes",
                       "(all)",
                       "unit",
                       "(all)",
                       "mean(us)",
                       "p50(us)",
                       "p99.99(us)")
                  << std::endl;
      }

      auto metric = action_();

      auto const elapsed_total_time = std::chrono::duration<double>(now - start_time_).count();
      auto const total_op_rate = metric.ops / elapsed_total_time;
      auto const total_msg_rate = metric.msgs / elapsed_total_time;
      auto const total_throughput = metric.bytes / elapsed_total_time;

      double current_op_rate = 0;
      double current_msg_rate = 0;
      double current_throughput = 0;

      auto const elapsed_since_last_check = std::chrono::duration<double>(now - last_time_checked_).count();

      if (elapsed_since_last_check > 0) // Avoid division by zero if interval is too small or first tick
      {
        current_op_rate = (metric.ops - last_metric_.ops) / elapsed_since_last_check;
        current_msg_rate = (metric.msgs - last_metric_.msgs) / elapsed_since_last_check;
        current_throughput = (metric.bytes - last_metric_.bytes) / elapsed_since_last_check;
      }
      else if (start_time_ == now)
      { // If it's the very first tick, current rate is the same as total rate
        current_op_rate = total_op_rate;
        current_msg_rate = total_msg_rate;
        current_throughput = total_throughput;
      }

      std::cout << std::format(
                     format_line,
                     pretty_print(current_op_rate),
                     pretty_print(total_op_rate),
                     pretty_print(current_msg_rate),
                     pretty_print(total_msg_rate),
                     pretty_print(current_throughput),
                     pretty_print(total_throughput),
                     pretty_print(current_throughput / current_op_rate),
                     pretty_print(total_throughput / total_op_rate),
                     metric.latency_hist ? pretty_print(::hdr_mean(metric.latency_hist.get()) / 1000.0, 2) : "na",
                     metric.latency_hist
                       ? pretty_print(::hdr_value_at_percentile(metric.latency_hist.get(), 50.0) / 1000.0, 2)
                       : "na",
                     metric.latency_hist
                       ? pretty_print(::hdr_value_at_percentile(metric.latency_hist.get(), 99.99) / 1000.0, 2)
                       : "na")
                << std::endl;

      last_metric_ = std::move(metric);
      last_time_checked_ = now;
    }
  }

  void metric_hud::collect(sample s, std::chrono::steady_clock::time_point now)
  {
    ++last_metric_.msgs;
    last_metric_.update_latency_histogram(s.recv_ts - s.send_ts);
    // std::cout << s.recv_ts - s.send_ts << " on hist " << last_metric_.latency_hist.get() << std::endl;

    if (start_time_ == std::chrono::steady_clock::time_point{} || (now - last_time_checked_ >= interval_))
    {
      constexpr std::string_view format_line =
        "{:>8}  {:>8}  {:>8}  {:>8}  {:>8}  {:>8}  {:>8}  {:>8} / {:8}";
      // Initialize start_time_ on the first actual tick
      if (start_time_ == std::chrono::steady_clock::time_point{})
      {
        start_time_ = now;
        std::cout << std::format(
                       format_line,
                       "msgs",
                       "mean(us)",
                       "p50(us)",
                       "p95(us)",
                       "p99(us)",
                       "p99.9(us)",
                       "p99.99(us)",
                       "min(us)",
                       "max(us)")
                  << std::endl;
      }

      auto const elapsed_total_time = std::chrono::duration<double>(now - start_time_).count();
      auto const total_msg_rate = last_metric_.msgs / elapsed_total_time;
      auto const hist = last_metric_.latency_hist.get();
      /* std::cout << ::hdr_value_at_percentile(hist, 50.0) << "xx " << ::hdr_value_at_percentile(hist, 50.0) / 1000
                << " xx " << std::fixed << std::setprecision(2) << ::hdr_value_at_percentile(hist, 50.0) / 1000
                << std::endl; */
      std::cout << std::format(
                     format_line,
                     pretty_print(total_msg_rate),
                     pretty_print(::hdr_mean(hist) / 1000.0, 2),
                     pretty_print(::hdr_value_at_percentile(hist, 50.0) / 1000.0, 2),
                     pretty_print(::hdr_value_at_percentile(hist, 95.0) / 1000.0, 2),
                     pretty_print(::hdr_value_at_percentile(hist, 99.0) / 1000.0, 2),
                     pretty_print(::hdr_value_at_percentile(hist, 99.9) / 1000.0, 2),
                     pretty_print(::hdr_value_at_percentile(hist, 99.99) / 1000.0, 2),
                     pretty_print(::hdr_min(hist) / 1000.0, 2),
                     pretty_print(::hdr_max(hist) / 1000.0, 2))
                << std::endl;

      last_time_checked_ = now;
    }
  }
}
