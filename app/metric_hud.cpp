#include "metric_hud.hpp"
#include <iostream>
#include <string>
#include <cstdint>

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

void metric_hud::run()
{
  start_time_ = std::chrono::steady_clock::now();
  last_time_checked_ = start_time_;

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    const auto metric = action_();

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_time = std::chrono::duration<double>(now - start_time_).count();
    const auto msg_rate = metric.msgs / elapsed_time;
    const auto throughput = metric.bytes / elapsed_time;

    std::cout << msg_rate << " " << throughput << std::endl;
    std::cout << "total rate\t: " << pretty_print(msg_rate) << " msgs/s, throughput: \t" << pretty_print(throughput)
              << " bytes/s" << std::endl;

    const auto elapsed_time_since_last_check = std::chrono::duration<double>(now - last_time_checked_).count();
    const auto msg_rate_since_last_check = (metric.msgs - last_metric_.msgs) / elapsed_time_since_last_check;
    const auto throughput_since_last_check = (metric.bytes - last_metric_.bytes) / elapsed_time_since_last_check;

    std::cout << "current rate:\t" << pretty_print(msg_rate_since_last_check) << " msgs/s, throughput: \t"
              << pretty_print(throughput_since_last_check) << " bytes/s" << std::endl;

    last_metric_ = metric;
    last_time_checked_ = now;
  }
}
