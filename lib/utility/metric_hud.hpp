#pragma once

#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <iostream>
#include <numeric>

namespace utility
{
  class metric_hud
  {
  public:
    struct metric
    {
      std::uint64_t msgs = 0;
      std::uint64_t bytes = 0;
    };

    metric_hud(std::chrono::seconds interval, std::function<metric()> action) : interval_(interval), action_(action) {}

    void tick();

  private:
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_time_checked_;
    metric last_metric_;
    const std::chrono::seconds interval_;
    std::function<metric()> action_;
  };
}
