#include "receiver.hpp"
#include <iostream>

namespace uring
{

  receiver::receiver(config cfg)
    : config_{std::move(cfg)},
      io_ctx_{config_.uring_depth},
      buffer_pool_{io_ctx_, config_.buffer_count, config_.buffer_size, config_.buffer_group_id},
      pending_connections_queue_{128}
  {
  }

  receiver::~receiver() { stop(); }

  void receiver::start()
  {
    thread_ = std::thread{[this] { run(); }};
  }

  void receiver::stop()
  {
    stop_flag_.store(true);

    if (thread_.joinable())
    {
      io_ctx_.wakeup();
      thread_.join();
      std::cout << "Receiver thread " << std::this_thread::get_id() << " joined." << std::endl;
    }
  }

  bool receiver::add_connection(bsd::socket&& sock)
  {
    if (!pending_connections_queue_.push(std::move(sock)))
    {
      std::cerr << "Receiver " << std::this_thread::get_id() << " queue is full. Dropping connection." << std::endl;
      return false;
    }

    io_ctx_.wakeup();
    return true;
  }

  void receiver::process_new_connections()
  {
    pending_connections_queue_.consume_all([this](bsd::socket sock) {
      try
      {
        std::cout << "Receiver thread " << std::this_thread::get_id() << " taking ownership of fd " << sock.get_fd()
                  << " from queue." << std::endl;
        auto& conn = connections_.emplace_front(io_ctx_, buffer_pool_);
        conn.open(std::move(sock));
        conn.start();
      }
      catch (const std::exception& e)
      {
        std::cerr << "Failed to create connection from accepted socket: " << e.what() << std::endl;
      }
    });
  }

  void receiver::run()
  {
    std::cout << "Receiver thread " << std::this_thread::get_id() << " started." << std::endl;
    
    try
    {
      while (!stop_flag_.load())
      {
        io_ctx_.poll();
        process_new_connections();
        connections_.remove_if([](const auto& conn) { return conn.is_closed(); });
      }
    }
    catch (const std::exception& e)
    {
      std::cerr << "Error in receiver thread: " << e.what() << std::endl;
    }

    std::cout << "Receiver thread " << std::this_thread::get_id() << " stopping." << std::endl;
  }

} // namespace uring