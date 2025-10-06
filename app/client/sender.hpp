#pragma once

#include "connection.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <climits> // For IOV_MAX

class sender
{
public:
  struct config
  {
    int conns;
    std::size_t msg_size;
    bool nodelay = false;
    bool drain = false;
    int socket_buffer_size = 0;
    int msgs_per_sec = 0;
    std::uint64_t stop_after_n_messages = 0;
    std::uint64_t stop_after_n_seconds = 0;
    std::size_t max_batch_size = IOV_MAX;
  };

  sender(int id, config const& cfg);

  void connect(std::string const& host, std::string const& port, std::string const& bind_address);
  void start(std::atomic<int>& shutdown_counter);
  void stop();

  std::uint64_t total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }
  std::uint64_t total_send_ops() const { return total_send_ops_.load(std::memory_order_relaxed); }

private:
  void run();
  void run_after_n_messages();
  void run_after_n_seconds();

  const config cfg_;
  std::vector<connection> conns_;
  std::atomic<bool> stop_flag_ = false;
  std::jthread _thread;
  std::chrono::steady_clock::duration interval_;
  std::chrono::steady_clock::time_point start_time_;
  std::atomic<std::uint64_t> total_send_ops_ = 0;
  std::atomic<std::uint64_t> total_msgs_sent_ = 0;
};
