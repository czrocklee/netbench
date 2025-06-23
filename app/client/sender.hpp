#pragma once

#include "connection.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstring> // For strerror, memset, memcpy
#include <cstdlib> // For atoi (though std::stoi is preferred)
#include <cerrno>  // For errno

// BSD socket headers
#include <sys/socket.h>
#include <netinet/in.h> // For sockaddr_in
#include <unistd.h>     // For close
#include <netdb.h>      // For gethostbyname, h_errno, herror

#include <thread>
#include <random>


class sender
{
public:
  sender(int id, int conns, int msgs_per_sec)
    : interval_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec}
  {
    conns_.reserve(conns);
    for (int i = 0; i < conns; ++i) { conns_.emplace_back(id * 1000 + i); }
  }

  void start(const std::string& host, const std::string& port, int msg_size)
  {
    message_.resize(msg_size, std::byte{'a'});

    for (auto& conn : conns_) { conn.connect(host, port); }

    static std::random_device rd;
    const auto start_time_ =
      std::chrono::steady_clock::now() +
      std::chrono::nanoseconds{std::uniform_int_distribution<std::int64_t>{0, interval_.count()}(rd)};

    _thread = std::jthread{[this] { run(); }};
  }

  std::uint64_t total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }

private:
  void run()
  {
    int conn_idx = 0;

    while (true)
    {
      const auto now = std::chrono::steady_clock::now();
      const auto expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);

      if (msgs_sent_ < expected_msgs)
      {
        conns_[conn_idx++].send(reinterpret_cast<const char*>(message_.data()), message_.size());
        total_msgs_sent_.store(total_msgs_sent_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        ++msgs_sent_;

        if (conn_idx == conns_.size())
        {
          conn_idx = 0;
        }
      }
    }
  }

  std::vector<connection> conns_;
  std::vector<std::byte> message_;
  std::jthread _thread;
  const std::chrono::steady_clock::duration interval_;
  std::chrono::steady_clock::time_point start_time_;
  std::uint64_t msgs_sent_ = 0;
  std::atomic<std::uint64_t> total_msgs_sent_ = 0;
};
