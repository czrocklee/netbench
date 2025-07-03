#include "receiver.hpp"
#include <iostream>

namespace uring
{

  receiver::receiver(config cfg)
    : config_{std::move(cfg)},
      io_ctx_{config_.uring_depth, config_.params},
      buffer_pool_{io_ctx_, config_.buffer_count, config_.buffer_size, config_.buffer_group_id},
      pending_task_queue_{128}
  {
    buffer_pool_.populate_buffers();
  }

  receiver::~receiver() { stop(); }

  void receiver::start()
  {
    thread_ = std::thread{[this] { run(); }};
  }

  void receiver::stop()
  {
    stop_flag_.store(true, std::memory_order::relaxed);

    if (thread_.joinable())
    {
      io_ctx_.wakeup();
      thread_.join();
      std::cout << "Receiver thread " << std::this_thread::get_id() << " joined." << std::endl;
    }
  }

  void receiver::add_connection(bsd::socket&& sock)
  {
    try
    {
      auto& conn = connections_.emplace_front(io_ctx_, buffer_pool_);
      conn.open(std::move(sock));
      conn.start();
    }
    catch (const std::exception& e)
    {
      std::cerr << "Failed to create connection from accepted socket: " << e.what() << std::endl;
    }
  }

  bool receiver::post(std::move_only_function<void()> task)
  {
    if (!pending_task_queue_.push(std::move(task)))
    {
      std::cerr << "Receiver " << std::this_thread::get_id() << "task queue is full" << std::endl;
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

  void receiver::process_pending_tasks()
  {
    pending_task_queue_.consume_all([this](std::move_only_function<void()>&& task) { task(); });
    connections_.remove_if([](const auto& conn) { return conn.is_closed(); });
  }

  void receiver::run()
  {
    std::cout << "Receiver thread " << std::this_thread::get_id() << " started." << std::endl;
    io_ctx_.enable();

    try
    {
      while (!stop_flag_.load(std::memory_order::relaxed))
      {
        io_ctx_.poll_wait();
        process_pending_tasks();
      }
    }
    catch (const std::exception& e)
    {
      std::cerr << "Error in receiver thread: " << e.what() << std::endl;
    }

    std::cout << "Receiver thread " << std::this_thread::get_id() << " stopping." << std::endl;
  }

} // namespace uring