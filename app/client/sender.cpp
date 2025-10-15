// Implementation for sender class/functions

#include "sender.hpp"
#include "utils.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

sender::sender(int id, config const& cfg) : cfg_{cfg}
{
  conns_.reserve(cfg_.conns);

  for (int i = 0; i < cfg_.conns; ++i)
  {
    conns_.emplace_back(id * 1000 + i, cfg_.msg_size);
  }

  if (cfg_.msgs_per_sec > 0)
  {
    interval_ = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / cfg_.msgs_per_sec;
  }
  else
  {
    // Unlimited: set a minimal interval to avoid division by zero, we'll treat it as "send as fast as possible"
    interval_ = std::chrono::nanoseconds{1};
  }

  for (auto& conn : conns_)
  {
    if (cfg_.drain)
    {
      conn.enable_drain();
    }

    if (cfg_.nodelay)
    {
      conn.set_nodelay(true);
    }

    if (cfg_.socket_buffer_size > 0)
    {
      conn.set_socket_buffer_size(cfg_.socket_buffer_size);
    }
  }
}

void sender::connect(std::string const& host, std::string const& port, std::string const& bind_address)
{
  for (auto& conn : conns_)
  {
    conn.connect(host, port, bind_address);
  }
}

void sender::start(std::atomic<int>& shutdown_counter, int cpu_id)
{
  static std::random_device rd;
  start_time_ = std::chrono::steady_clock::now() +
                std::chrono::nanoseconds{std::uniform_int_distribution<std::int64_t>{0, interval_.count()}(rd)};

  _thread = std::jthread{[this, &shutdown_counter, cpu_id] {
    if (cpu_id >= 0)
    {
      set_thread_cpu_affinity(cpu_id);
    }

    auto const end_time = (cfg_.stop_after_n_seconds > 0)
                            ? (start_time_ + std::chrono::seconds{cfg_.stop_after_n_seconds})
                            : std::chrono::steady_clock::time_point::max();

    int conn_idx = 0;
    std::uint64_t msgs_sent = total_msgs_sent_.load(std::memory_order_relaxed);

    while (!stop_flag_.load(std::memory_order_relaxed))
    {
      if (std::chrono::steady_clock::now() >= end_time) [[unlikely]]
      {
        break;
      }

      if ((cfg_.stop_after_n_messages > 0 && msgs_sent >= cfg_.stop_after_n_messages)) [[unlikely]]
      {
        break;
      }

      std::uint64_t count = cfg_.max_batch_size;

      if (cfg_.msgs_per_sec > 0)
      {
        auto const now = std::chrono::steady_clock::now();
        auto const expected = static_cast<std::uint64_t>((now - start_time_) / interval_);

        if (expected <= msgs_sent)
        {
          std::this_thread::sleep_for(std::chrono::nanoseconds{50});
          continue;
        }

        count = std::max<std::uint64_t>(std::min<std::uint64_t>((expected - msgs_sent) / conns_.size(), count), 1);
      }

      if (cfg_.stop_after_n_messages > 0)
      {
        auto const remain = cfg_.stop_after_n_messages - msgs_sent;
        count = std::min(count, remain);
      }

      if (auto const sent = conns_[conn_idx].try_send(count); sent > 0)
      {
        msgs_sent += sent;
        total_msgs_sent_.store(msgs_sent, std::memory_order_relaxed);
      }

      total_send_ops_.store(total_send_ops_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

      if (++conn_idx == static_cast<int>(conns_.size()))
      {
        conn_idx = 0;
      }
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
