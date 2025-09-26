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
  sender(int id, int conns, std::size_t msg_size);

  void connect(std::string const& host, std::string const& port, std::string const& bind_address);
  void start(std::atomic<int>& shutdown_counter);
  void stop();
  
  void enable_drain();
  void set_nodelay(bool enable);
  void set_socket_buffer_size(int size);

  void set_message_rate(int msgs_per_sec);
  void stop_after_n_messages(std::uint64_t n) { stop_after_n_messages_ = n; }
  void stop_after_n_seconds(std::uint64_t n) { stop_after_n_seconds_ = n; }

  std::uint64_t total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }
  std::uint64_t total_send_ops() const { return total_send_ops_.load(std::memory_order_relaxed); }

private:
  void run();
  void run_after_n_messages();
  void run_after_n_seconds();

  std::vector<connection> conns_;
  std::atomic<bool> stop_flag_ = false;
  std::jthread _thread;
  std::chrono::steady_clock::duration interval_;
  std::chrono::steady_clock::time_point start_time_;
  std::atomic<std::uint64_t> total_send_ops_ = 0;
  std::atomic<std::uint64_t> total_msgs_sent_ = 0;
  std::uint64_t stop_after_n_messages_ = 0;
  std::uint64_t stop_after_n_seconds_ = 0;
};
