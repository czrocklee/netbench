#include "metric_hud.hpp"
#include <iostream>
#include <string>
#include <cstdint>

namespace
{
  std::string pretty_print_uint(std::uint64_t value)
  {
    if (value < 1000)
    {
      // No suffix needed, just convert to string
      return std::to_string(value);
    }

    const char* suffixes[] = {"k", "M", "G", "T", "P", "E"}; // Added Peta and Exa
    int suffix_index = -1;
    double temp_value = static_cast<double>(value);

    while (temp_value >= 1000.0 && suffix_index < 5)
    {
      temp_value /= 1000.0;
      suffix_index++;
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << temp_value << suffixes[suffix_index];
    return ss.str();
  }
}

void metric_hud::run()
{
  start_time_ = std::chrono::steady_clock::now();
  last_time_checked_ = start_time_;

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    const auto metric = action_();
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_time_millis = std::chrono::round<std::chrono::milliseconds>(now - start_time_).count();
    const auto msg_rate = metric.msgs / elapsed_time_millis * 1000;
    const auto throughput = metric.bytes / elapsed_time_millis * 1000;

    std::cout << msg_rate << " " << throughput << std::endl;
    std::cout << "total rate\t: " << pretty_print_uint(msg_rate)
              << " msgs/s, throughput: \t" << pretty_print_uint(throughput) << " bytes/s" << std::endl;

    const auto elapsed_time_millis_since_last_check =
      std::chrono::round<std::chrono::milliseconds>(now - last_time_checked_).count();
    const auto msg_rate_since_last_check =
      (metric.msgs - last_metric_.msgs) / elapsed_time_millis_since_last_check * 1000;
    const auto throughput_since_last_check =
      (metric.bytes - last_metric_.bytes) / elapsed_time_millis_since_last_check * 1000;

    std::cout << "current rate:\t" << pretty_print_uint(msg_rate_since_last_check)
              << " msgs/s, throughput: \t" << pretty_print_uint(throughput_since_last_check) << " bytes/s" << std::endl;

    last_metric_ = metric;
    last_time_checked_ = now; 
  }
}
