#pragma once

#include <thread>

#include <pthread.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <string>
#include <stdexcept>

inline void parse_address(std::string const& full_address, std::string& host, std::string& port)
{
  auto colon_pos = full_address.find(':');

  if (colon_pos == std::string::npos)
  {
    throw std::runtime_error{"Invalid address format. Expected host:port"};
  }

  host = full_address.substr(0, colon_pos);
  port = full_address.substr(colon_pos + 1);
}

inline void set_thread_cpu_affinity(std::thread::native_handle_type thread_handle, int cpu_id)
{
  if (cpu_id < 0)
  {
    return;
  }

  ::cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_id, &cpuset);

  if (::pthread_setaffinity_np(thread_handle, sizeof(cpu_set_t), &cpuset) != 0)
  {
    throw std::runtime_error{std::format("Failed to set thread affinity for cpu {}: {}", cpu_id, strerror(errno))};
  }
}

inline void set_thread_cpu_affinity(int cpu_id)
{
  set_thread_cpu_affinity(::pthread_self(), cpu_id);
}