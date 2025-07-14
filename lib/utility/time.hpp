#pragma once

#include <chrono>
#include <cstdint>

namespace utility
{
  inline std::uint64_t nanos_since_epoch()
  {
    auto const now = std::chrono::system_clock::now();
    auto const nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    return nanos.count();
  }
}
