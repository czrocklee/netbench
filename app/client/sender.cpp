// Implementation for sender class/functions

#include "sender.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

sender::sender(int id, int conns, std::size_t msg_size, int msgs_per_sec)
  : interval_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec}
{
  conns_.reserve(conns);

  for (int i = 0; i < conns; ++i)
  {
    conns_.emplace_back(id * 1000 + i, msg_size);
  }
}

void sender::start(std::string const& host, std::string const& port, std::string const& bind_address, bool nodelay)
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

void sender::enable_drain()
{
  for (auto& conn : conns_)
  {
    conn.enable_drain();
  }
}

void sender::run()
{
  int conn_idx = 0;

  while (true)
  {
    auto const now = std::chrono::steady_clock::now();
    auto const expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);

    for (auto msgs_sent = total_msgs_sent_.load(std::memory_order_relaxed); msgs_sent < expected_msgs;)
    {
      auto count = std::max((expected_msgs - msgs_sent) / conns_.size(), 1ul);

      if (auto sent = conns_[conn_idx++].try_send(count); sent > 0)
      {
        msgs_sent += sent;
        total_msgs_sent_.store(msgs_sent, std::memory_order_relaxed);
      }

      total_send_ops_.store(total_send_ops_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

      if (conn_idx == conns_.size())
      {
        conn_idx = 0;
      }
    }
  }
}
