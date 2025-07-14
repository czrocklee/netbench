#include "metric_hud.hpp"
#include <iostream>
#include <string>
#include <cstdint>
#include <iomanip> // For std::fixed, std::setprecision
#include <sstream> // For std::stringstream

namespace utility
{
  namespace
  {
    std::string pretty_print(double value)
    {
      if (std::abs(value) < 1000.0)
      {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << value;
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
      ss << std::fixed << std::setprecision(1) << temp_value << suffixes[suffix_index];
      return ss.str();
    }
  }

  void metric_hud::tick()
  {
    auto const now = std::chrono::steady_clock::now();

    // Only update if the interval has passed, or if it's the very first tick
    if (start_time_ == std::chrono::steady_clock::time_point{} || (now - last_time_checked_ >= interval_))
    {
      // Initialize start_time_ on the first actual tick
      if (start_time_ == std::chrono::steady_clock::time_point{})
      {
        start_time_ = now;
      }

      auto const metric = action_();

      auto const elapsed_total_time = std::chrono::duration<double>(now - start_time_).count();
      auto const total_op_rate = metric.ops / elapsed_total_time;
      auto const total_throughput = metric.bytes / elapsed_total_time;

      std::cout << "total rate:\t" << pretty_print(total_op_rate) << " ops/s \tthroughput: \t"
                << pretty_print(total_throughput)
                << " bytes/s \tunit:\t" << pretty_print(total_throughput / total_op_rate) << " bytes/op" << std::endl;

      // Calculate and print current rate (since last check)
      auto const elapsed_since_last_check = std::chrono::duration<double>(now - last_time_checked_).count();
      if (elapsed_since_last_check > 0) // Avoid division by zero if interval is too small or first tick
      {
        auto const current_op_rate = (metric.ops - last_metric_.ops) / elapsed_since_last_check;
        auto const current_throughput = (metric.bytes - last_metric_.bytes) / elapsed_since_last_check;

        std::cout << "current rate:\t" << pretty_print(current_op_rate) << " ops/s\tthroughput: \t"
                  << pretty_print(current_throughput)
                  << " bytes/s\tunit:\t" << pretty_print(current_throughput / current_op_rate) << " bytes/op"
                  << std::endl;
      }
      else if (start_time_ == now)
      { // If it's the very first tick, current rate is the same as total rate
        std::cout << "current rate:\t" << pretty_print(total_op_rate) << " ops/s\tthroughput: \t"
                  << pretty_print(total_throughput)
                  << " bytes/s\tunit:\t" << pretty_print(total_throughput / total_op_rate) << " bytes/op" << std::endl;
      }

      last_metric_ = metric;
      last_time_checked_ = now;
    }
  }
}
