#pragma once

#include "bsd/socket.hpp"
#include <liburing.h> // Use the raw liburing header

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
    uring_sender(int id, const std::string& host, const std::string& port, int msg_size, int msgs_per_sec);

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

    void on_send_complete(::io_uring_cqe* cqe);

    int id_;
    ::io_uring ring_; // Direct io_uring instance
    bsd::socket conn_;
    std::vector<std::byte> message_;
    std::chrono::nanoseconds interval_;
    std::jthread thread_;
    struct iovec iovec_;

    std::atomic<uint64_t> total_msgs_completed_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
  };

} // namespace client
