#include "worker.hpp"
#include "utility/time.hpp"

#include <iostream>

worker::worker(config cfg)
  : config_{std::move(cfg)},
#ifdef IO_URING_API
    io_ctx_{config_.uring_depth, config_.params},
    buffer_pool_{io_ctx_, config_.buffer_count, config_.buffer_size, uring::provided_buffer_pool::group_id_type{0}},
#elifdef ASIO_API
    io_ctx_{1},
    work_guard_{::asio::make_work_guard(io_ctx_)},
#endif
    pending_task_queue_{128}
{
  metrics_.init_histogram();
#ifdef IO_URING_API
  buffer_pool_.populate_buffers();
#endif
}

worker::~worker()
{
  stop();
}

void worker::start(bool busy_spin)
{
  thread_ = std::thread{[this, busy_spin] { busy_spin ? run_busy_spin() : run(); }};
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
    iter->receiver.set_read_limit(config_.read_limit);
#endif

    iter->sender.open(sock.native_handle());
    iter->receiver.open(std::move(sock));
    iter->receiver.start([this, iter](std::error_code ec, asio::const_buffer const data) {
      if (ec)
      {
        std::cerr << "Error receiving data: " << ec.message() << std::endl;
        connections_.erase(iter);
        return;
      }

      on_data(*iter, data);
    });
  }
  catch (std::exception const& e)
  {
    std::cerr << "Failed to create connection from accepted socket: " << e.what() << std::endl;
  }
}

void worker::send_first_message()
{
    if (connections_.empty())
    {
        return;
    }
    auto& conn = connections_.front();
    auto buffer = std::make_unique<std::byte[]>(config_.buffer_size);
    conn.sender.send(asio::buffer(buffer.get(), config_.buffer_size), [this, &conn, buffer = std::move(buffer)](std::error_code ec, std::size_t bytes_transferred) {
        if (ec)
        {
            std::cerr << "Error sending data: " << ec.message() << std::endl;
            return;
        }
        metrics_.bytes += bytes_transferred;
        metrics_.ops++;
        metrics_.msgs++;
    });
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

void worker::on_data(connection& conn, asio::const_buffer const data)
{
    conn.sender.send(data, [this](std::error_code ec, std::size_t bytes_transferred) {
        if (ec)
        {
            std::cerr << "Error sending data: " << ec.message() << std::endl;
            return;
        }
        metrics_.bytes += bytes_transferred;
        metrics_.ops++;
        metrics_.msgs++;
    });
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

void worker::run_busy_spin()
{
  std::cout << "worker thread " << std::this_thread::get_id() << " started with busy spin polling." << std::endl;

#ifdef IO_URING_API
  io_ctx_.enable();
#endif

  try
  {
#ifdef ASIO_API
    while (!stop_flag_.load(std::memory_order::relaxed)) { io_ctx_.poll(); }
#else
    while (!stop_flag_.load(std::memory_order::relaxed))
    {
      for (auto i = 0; i < 1000; ++i) { io_ctx_.poll(); }

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
