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

      const char* suffixes[] = {"", "k", "M", "G", "T"};
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
    const auto now = std::chrono::steady_clock::now();

    // Only update if the interval has passed, or if it's the very first tick
    if (start_time_ == std::chrono::steady_clock::time_point{} || (now - last_time_checked_ >= interval_))
    {
      // Initialize start_time_ on the first actual tick
      if (start_time_ == std::chrono::steady_clock::time_point{})
      {
        start_time_ = now;
      }

      const auto metric = action_();

      const auto elapsed_total_time = std::chrono::duration<double>(now - start_time_).count();
      const auto total_msg_rate = metric.msgs / elapsed_total_time;
      const auto total_throughput = metric.bytes / elapsed_total_time;

      std::cout << "total rate\t: " << pretty_print(total_msg_rate) << " msgs/s, throughput: \t"
                << pretty_print(total_throughput) << " bytes/s" << std::endl;

      // Calculate and print current rate (since last check)
      const auto elapsed_since_last_check = std::chrono::duration<double>(now - last_time_checked_).count();
      if (elapsed_since_last_check > 0) // Avoid division by zero if interval is too small or first tick
      {
        const auto current_msg_rate = (metric.msgs - last_metric_.msgs) / elapsed_since_last_check;
        const auto current_throughput = (metric.bytes - last_metric_.bytes) / elapsed_since_last_check;

        std::cout << "current rate:\t" << pretty_print(current_msg_rate) << " msgs/s, throughput: \t"
                  << pretty_print(current_throughput) << " bytes/s" << std::endl;
      }
      else if (start_time_ == now)
      { // If it's the very first tick, current rate is the same as total rate
        std::cout << "current rate:\t" << pretty_print(total_msg_rate) << " msgs/s, throughput: \t"
                  << pretty_print(total_throughput) << " bytes/s" << std::endl;
      }

      last_metric_ = metric;
      last_time_checked_ = now;
    }
  }
}
