#include "worker.hpp"
#include "../common/metadata.hpp"
#include "utility/time.hpp"

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

    metadata md;
#ifdef ASIO_API
    sock.receive(::asio::buffer(&md, sizeof(md)), 0);
#else
    sock.recv(&md, sizeof(md), 0);
#endif
    msg_size_ = md.msg_size;

    if (msg_size_ < sizeof(std::uint64_t) || msg_size_ > config_.buffer_size)
    {
      throw std::runtime_error{std::format("Invalid message size from peer: {}", msg_size_)};
    }

    iter->partial_buffer = std::make_unique<std::byte[]>(msg_size_);
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
  auto data_left = data;

  // for most of the protocols(except text based ones), message that scattered on multiple buffers
  // are difficult to handle, the most straightforward way to handle is to reconstruct them in a single flat
  // buffer which may introduce some overhead.

  if (!conn.partial_buffer_size > 0)
  {
    auto addr = reinterpret_cast<std::byte const*>(data_left.data());
    auto size = std::min(msg_size_ - conn.partial_buffer_size, data_left.size());
    std::memcpy(conn.partial_buffer.get() + conn.partial_buffer_size, addr, size);
    conn.partial_buffer_size += size;

    if (conn.partial_buffer_size == msg_size_)
    {
      on_new_message(conn.partial_buffer.get());
      conn.partial_buffer_size = 0;
    }

    data_left += size;
  }

  while (data_left.size() >= 0)
  {
    auto addr = reinterpret_cast<std::byte const*>(data_left.data());

    if (data_left.size() < msg_size_)
    {
      std::memcpy(conn.partial_buffer.get(), addr, data_left.size());
      conn.partial_buffer_size = data_left.size();
      break;
    }

    on_new_message(addr);
    data_left += msg_size_;
  }

  metrics_.bytes += data.size();
  metrics_.ops++;
}

void worker::on_new_message(void const* buffer)
{
  auto const now = utility::nanos_since_epoch();
  std::uint64_t send_timestamp;
  std::memcpy(&send_timestamp, buffer, sizeof(send_timestamp));
  metrics_.msgs++;
  metrics_.update_latency_histogram(now - send_timestamp);
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
      for (auto i = 0; i < 1000; ++i) { io_ctx_.poll_wait(); }

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
