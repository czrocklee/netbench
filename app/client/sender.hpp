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
  sender(int id, int conns, std::size_t msg_size, int msgs_per_sec)
    : interval_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec}
  {
    conns_.reserve(conns);
    for (int i = 0; i < conns; ++i) { conns_.emplace_back(id * 1000 + i, msg_size); }
  }

  void
  start(std::string const& host, std::string const& port, std::string const& bind_address, int msg_size, bool nodelay)
  {
    for (auto& conn : conns_)
    {
      conn.connect(host, port, bind_address);
      conn.set_nodelay(nodelay);
    }

    static std::random_device rd;
    start_time_ = std::chrono::steady_clock::now() +
                  std::chrono::nanoseconds{std::uniform_int_distribution<std::int64_t>{0, interval_.count()}(rd)};

    _thread = std::jthread{[this] { run(); }};
  }

  void enable_drain()
  {
    //io_ctx_ = std::make_unique<bsd::io_context>(1024);
    for (auto& conn : conns_) { conn.enable_drain(); }
  }

  std::uint64_t total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }

  std::uint64_t total_send_ops() const { return total_send_ops_.load(std::memory_order_relaxed); }

private:
  void run()
  {
    int conn_idx = 0;

    while (true)
    {
      auto const now = std::chrono::steady_clock::now();
      auto const expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);
      // std::cout << interval_.count() << " " << (now - start_time_).count() << " " << expected_msgs << std::endl;

      for (auto msgs_sent = total_msgs_sent_.load(std::memory_order_relaxed); msgs_sent < expected_msgs;)
      {
        if (conns_[conn_idx++].try_send()) { total_msgs_sent_.store(++msgs_sent, std::memory_order_relaxed); }

        total_send_ops_.store(total_send_ops_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

        if (conn_idx == conns_.size()) { conn_idx = 0; }
      }
    }
  }

  std::vector<connection> conns_;
  std::jthread _thread;
  std::chrono::steady_clock::duration const interval_;
  std::chrono::steady_clock::time_point start_time_;
  std::atomic<std::uint64_t> total_send_ops_ = 0;
  std::atomic<std::uint64_t> total_msgs_sent_ = 0;
};
