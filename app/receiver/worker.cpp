#include "worker.hpp"
#include <iostream>

worker::worker(config cfg)
  : config_{std::move(cfg)},
#ifdef IO_URING_API
    io_ctx_{config_.uring_depth, config_.params},
    buffer_pool_{
      io_ctx_,
      config_.buffer_count,
      config_.buffer_size,
      uring::provided_buffer_pool::group_id_type{config_.buffer_group_id}},
#elifdef ASIO_API
    io_ctx_{1},
    work_guard_{::asio::make_work_guard(io_ctx_)},
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
#ifndef ASIO_API
    io_ctx_.wakeup();
#else // ASIO_API
    io_ctx_.stop();
#endif
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
#else
    auto iter = connections_.emplace(connections_.begin(), io_ctx_, config_.buffer_size);
#endif

#ifdef BSD_API
    iter->set_read_limit(config_.read_limit);
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
      metrics_.ops++;
    });
  }
  catch (std::exception const& e)
  {
    std::cerr << "Failed to create connection from accepted socket: " << e.what() << std::endl;
  }
}

bool worker::post(std::move_only_function<void()> task)
{
#ifndef ASIO_API
  if (!pending_task_queue_.push(std::move(task)))
  {
    std::cerr << "worker " << std::this_thread::get_id() << "task queue is full" << std::endl;
    return false;
  }

  io_ctx_.wakeup();
#else  // ASIO_API
  ::asio::post(io_ctx_, std::move(task));
#endif // ASIO_API
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
#ifdef ASIO_API
    io_ctx_.run();
#else
    while (!stop_flag_.load(std::memory_order::relaxed))
    {
      io_ctx_.poll_wait();
      process_pending_tasks();
    }
#endif
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error in worker thread: " << e.what() << std::endl;
  }

  std::cout << "worker thread " << std::this_thread::get_id() << " stopping." << std::endl;
}
