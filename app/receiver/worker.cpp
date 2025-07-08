#include "worker.hpp"
#include <iostream>

worker::worker(config cfg)
  : config_{std::move(cfg)},
#ifdef IO_URING_API
    io_ctx_{config_.uring_depth, config_.params},
    buffer_pool_{io_ctx_, config_.buffer_count, config_.buffer_size, config_.buffer_group_id},
#elifdef ASIO_API
#ifdef ASIO_HAS_IO_URING
    io_ctx_{asio::io_uring_context{asio::io_uring_context::params{.entries = 1024}}.get_executor()}, // Set to a reasonable value
#else
    work_guard_{::asio::make_work_guard(io_ctx_)},
#endif // ASIO_HAS_IO_URING
#else  // BSD_API
    io_ctx_{1},
#endif
    pending_task_queue_{128}
{
#ifdef IO_URING_API
  buffer_pool_.populate_buffers();
#endif
}

worker::~worker() { stop(); }

void worker::start()
{
  thread_ = std::thread{[this] { run(); }};
}

void worker::stop()
{
  stop_flag_.store(true, std::memory_order::relaxed);

  if (thread_.joinable())
  {
    // io_ctx_.wakeup();
    thread_.join();
    std::cout << "worker thread " << std::this_thread::get_id() << " joined." << std::endl;
  }
}

void worker::add_connection(net::socket sock)
{
  try
  {
#ifdef IO_URING_API
    auto iter = connections_.emplace(connections_.begin(), io_ctx_, buffer_pool_);
#else // BSD_API
    auto iter = connections_.emplace(connections_.begin(), io_ctx_, config_.buffer_size);
#endif
    iter->open(std::move(sock));
    iter->start([this, iter](std::error_code ec, asio::const_buffer data) {
      if (ec)
      {
        std::cerr << "Error receiving data: " << ec.message() << std::endl;
        connections_.erase(iter);
        return;
      }

      // Process the received data
      metrics_.bytes += data.size();
      metrics_.msgs++;
    });
  }
  catch (std::exception const& e)
  {
    std::cerr << "Failed to create connection from accepted socket: " << e.what() << std::endl;
  }
}

bool worker::post(std::move_only_function<void()> task)
{
  if (!pending_task_queue_.push(std::move(task)))
  {
    std::cerr << "worker " << std::this_thread::get_id() << "task queue is full" << std::endl;
    return false;
  }

  // io_ctx_.wakeup();
  return true;
}

void worker::process_pending_tasks()
{
  pending_task_queue_.consume_all([this](std::move_only_function<void()>&& task) { task(); });
}

void worker::run()
{
  std::cout << "worker thread " << std::this_thread::get_id() << " started." << std::endl;

#ifdef IO_URING_API
  io_ctx_.enable();
#endif

  try
  {
    while (!stop_flag_.load(std::memory_order::relaxed))
    {
#ifdef ASIO_API
      io_ctx_.poll_one();
#else
      io_ctx_.poll();
#endif
      process_pending_tasks();
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error in worker thread: " << e.what() << std::endl;
  }

  std::cout << "worker thread " << std::this_thread::get_id() << " stopping." << std::endl;
}
