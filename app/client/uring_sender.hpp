#pragma once

#include "bsd/socket.hpp"
#include <liburing.h> // Use the raw liburing header
#include "uring/io_context.hpp"
#include "uring/provided_buffer_pool.hpp"
#include "uring/sender.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstddef>

namespace client
{
  class uring_sender
  {
  public:
    uring_sender(int id, int conns, const std::string& host, const std::string& port, std::size_t msg_size, int msgs_per_sec);

    ~uring_sender();

    uring_sender(const uring_sender&) = delete;
    uring_sender& operator=(const uring_sender&) = delete;
    uring_sender(uring_sender&&) = delete;
    uring_sender& operator=(uring_sender&&) = delete;

    void start();

    uint64_t total_msgs_sent() const;

    uint64_t total_bytes_sent() const;

  private:
    void run();

    int id_;
    ::io_uring_params uring_params_;
    uring::io_context io_ctx_;
    std::deque<uring::sender> senders_;


    std::chrono::steady_clock::time_point start_time_;
    std::chrono::nanoseconds interval_;
    std::jthread thread_;

    std::atomic<uint64_t> total_msgs_sent_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
  };

} // namespace client
