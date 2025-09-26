// Implementation for sender class/functions

#include "sender.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

sender::sender(int id, int conns, std::size_t msg_size)
{
  conns_.reserve(conns);

  for (int i = 0; i < conns; ++i)
  {
    conns_.emplace_back(id * 1000 + i, msg_size);
  }
}

void sender::connect(std::string const& host, std::string const& port, std::string const& bind_address)
{
  for (auto& conn : conns_)
  {
    conn.connect(host, port, bind_address);
  }
}

void sender::start(std::atomic<int>& shutdown_counter)
{
  static std::random_device rd;
  start_time_ = std::chrono::steady_clock::now() +
                std::chrono::nanoseconds{std::uniform_int_distribution<std::int64_t>{0, interval_.count()}(rd)};

  _thread = std::jthread{[this, &shutdown_counter] {
    if (stop_after_n_messages_ > 0)
    {
      run_after_n_messages();
    }
    else if (stop_after_n_seconds_ > 0)
    {
      run_after_n_seconds();
    }
    else
    {
      run();
    }

    for (auto& conn : conns_)
    {
      conn.close();
    }

    shutdown_counter.fetch_sub(1);
  }};
}

void sender::stop()
{
  stop_flag_.store(true, std::memory_order_relaxed);

  if (_thread.joinable())
  {
    _thread.join();
  }
}

void sender::enable_drain()
{
  for (auto& conn : conns_)
  {
    conn.enable_drain();
  }
}

void sender::set_nodelay(bool enable)
{
  for (auto& conn : conns_)
  {
    conn.set_nodelay(enable);
  }
}

void sender::set_message_rate(int msgs_per_sec)
{
  interval_ = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec;
}

void sender::set_socket_buffer_size(int size)
{
  for (auto& conn : conns_)
  {
    conn.set_socket_buffer_size(size);
  }
}

void sender::run()
{
  int conn_idx = 0;

  while (!stop_flag_.load(std::memory_order_relaxed))
  {
    auto const now = std::chrono::steady_clock::now();
    auto const expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);

    for (auto msgs_sent = total_msgs_sent_.load(std::memory_order_relaxed); msgs_sent < expected_msgs;)
    {
      auto count = std::max((expected_msgs - msgs_sent) / conns_.size(), 1ul);

      if (auto const sent = conns_[conn_idx++].try_send(count); sent > 0)
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

void sender::run_after_n_messages()
{
  int conn_idx = 0;

  while (!stop_flag_.load(std::memory_order_relaxed))
  {
    auto const msgs_sent = total_msgs_sent_.load(std::memory_order_relaxed);

    if (msgs_sent >= stop_after_n_messages_)
    {
      break;
    }

    if (auto const sent = conns_[conn_idx++].try_send(stop_after_n_messages_ - msgs_sent); sent > 0)
    {
      total_msgs_sent_.store(msgs_sent + sent, std::memory_order_relaxed);
    }

    total_send_ops_.store(total_send_ops_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    if (conn_idx == conns_.size())
    {
      conn_idx = 0;
    }
  }
}

void sender::run_after_n_seconds()
{
  int conn_idx = 0;
  auto const end_time = start_time_ + std::chrono::seconds{stop_after_n_seconds_};

  while (!stop_flag_.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < end_time)
  {
    if (auto const sent = conns_[conn_idx++].try_send(std::numeric_limits<std::uint64_t>::max()); sent > 0)
    {
      total_msgs_sent_.store(total_msgs_sent_.load(std::memory_order_relaxed) + sent, std::memory_order_relaxed);
    }

    total_send_ops_.store(total_send_ops_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    if (conn_idx == conns_.size())
    {
      conn_idx = 0;
    }
  }
}
