#include "receiver.hpp"
#include <iostream>
#include <utility>

namespace bsd
{
  receiver::receiver(config cfg)
    : config_{std::move(cfg)},
      io_ctx_{config_.epoll_size},
      pending_connections_queue_{128},
      pending_task_queue_{128}
  {
  }

  receiver::~receiver() { stop(); }

  void receiver::start() { thread_ = std::thread{[this] { run(); }}; }

  void receiver::stop()
  {
    stop_flag_.store(true, std::memory_order::relaxed);
    if (thread_.joinable())
    {
      io_ctx_.wakeup();
      thread_.join();
    }
  }

  bool receiver::add_connection(bsd::socket&& sock)
  {
    if (!pending_connections_queue_.push(std::move(sock)))
    {
      return false;
    }
    io_ctx_.wakeup();
    return true;
  }

  bool receiver::post(std::function<void()> task)
  {
    if (!pending_task_queue_.push(std::move(task)))
    {
      return false;
    }
    io_ctx_.wakeup();
    return true;
  }

  utility::metric_hud::metric receiver::get_metrics()
  {
    utility::metric_hud::metric total{};
    for (const auto& conn : connections_)
    {
      const auto& conn_metrics = conn.get_metrics();
      total.msgs += conn_metrics.msgs;
      total.bytes += conn_metrics.bytes;
    }
    return total;
  }

  void receiver::process_new_connections_and_tasks()
  {
    pending_connections_queue_.consume_all([this](bsd::socket&& sock) {
      try
      {
        connections_.emplace_front(io_ctx_, config_.buffer_size).open(std::move(sock));
        connections_.front().start();
      }
      catch (const std::exception& e)
      {
        std::cerr << "Failed to create connection: " << e.what() << std::endl;
      }
    });

    pending_task_queue_.consume_all([](std::function<void()>&& task) { task(); });
  }

  void receiver::run()
  {
    std::cout << "BSD Receiver thread " << std::this_thread::get_id() << " started." << std::endl;
    try
    {
      while (!stop_flag_.load(std::memory_order::relaxed))
      {
        io_ctx_.poll();
        process_new_connections_and_tasks();
        connections_.remove_if([](const auto& conn) { return conn.is_closed(); });
      }
    }
    catch (const std::exception& e)
    {
      std::cerr << "Error in bsd receiver thread: " << e.what() << std::endl;
    }
    std::cout << "BSD Receiver thread " << std::this_thread::get_id() << " stopping." << std::endl;
  }
}