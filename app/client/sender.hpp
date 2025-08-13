#pragma once

#include "connection.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>

class sender
{
public:
  sender(int id, int conns, std::size_t msg_size, int msgs_per_sec);
  
  void start(std::string const& host, std::string const& port, std::string const& bind_address, bool nodelay);
  void enable_drain();

  std::uint64_t total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }
  std::uint64_t total_send_ops() const { return total_send_ops_.load(std::memory_order_relaxed); }

private:
  void run();
  std::vector<connection> conns_;
  std::jthread _thread;
  std::chrono::steady_clock::duration const interval_;
  std::chrono::steady_clock::time_point start_time_;
  std::atomic<std::uint64_t> total_send_ops_ = 0;
  std::atomic<std::uint64_t> total_msgs_sent_ = 0;
};
