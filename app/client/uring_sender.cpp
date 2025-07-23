#include "uring_sender.hpp"
#include <utility/time.hpp>

#include <iostream>
#include <numeric>
#include <random>

namespace client
{

  namespace
  {
    ::io_uring_params uring_params()
    {
      io_uring_params params{};
      params.cq_entries = 65536;
      params.flags |= IORING_SETUP_R_DISABLED;
      params.flags |= IORING_SETUP_SINGLE_ISSUER;
      // params.flags |= IORING_SETUP_DEFER_TASKRUN;
      params.flags |= (IORING_SETUP_COOP_TASKRUN | IORING_SETUP_TASKRUN_FLAG);
      return params;
    }

  }

  uring_sender::uring_sender(
    int id,
    int conns,
    std::string const& host,
    std::string const& port,
    std::size_t msg_size,
    std::size_t send_queue_size,
    int msgs_per_sec,
    bool nodelay)
    : id_{id},
      uring_params_{uring_params()},
      io_ctx_{65536 / 2, uring_params_},
      interval_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec}
  {
    for (int i = 0; i < conns; ++i)
    {
      auto& sender = senders_.emplace_back(
        io_ctx_, send_queue_size, msg_size, uring::provided_buffer_pool::group_id_type::cast_from(id * 100 + i));

      auto& buffer_pool = sender.get_buffer_pool();

      for (auto i = uring::provided_buffer_pool::buffer_id_type{0}; i < buffer_pool.get_buffer_count(); ++i)
      {
        auto* buffer = buffer_pool.get_buffer_address(i);
        std::memset(buffer, 'a' + i % 26, msg_size);
      }
    }

    for (auto& sender : senders_)
    {
      bsd::socket sock(AF_INET, SOCK_STREAM, 0);
      sock.connect(host, port);
      metadata md{.msg_size = msg_size};
      sock.send(&md, sizeof(md), 0);
      sock.set_nodelay(nodelay);
      sender.open(std::move(sock));
    }
  }

  uring_sender::~uring_sender() {}

  void uring_sender::start()
  {
    static std::random_device rd;
    start_time_ = std::chrono::steady_clock::now() +
                  std::chrono::nanoseconds{std::uniform_int_distribution<std::int64_t>{0, interval_.count()}(rd)};

    thread_ = std::jthread{[this] { run(); }};
  }

  uint64_t uring_sender::total_msgs_sent() const
  {
    return total_msgs_sent_.load(std::memory_order_relaxed);
  }

  uint64_t uring_sender::total_bytes_sent() const
  {
    return total_bytes_sent_.load(std::memory_order_relaxed);
  }

  void uring_sender::run()
  {
    io_ctx_.enable();
    int conn_idx = 0;

    while (true)
    {
      auto const now = std::chrono::steady_clock::now();
      auto const expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);
      // std::cout << interval_.count() << " " << (now - start_time_).count() << " " << expected_msgs << std::endl;
      auto msgs_sent = total_msgs_sent_.load(std::memory_order_relaxed);

      while (msgs_sent < expected_msgs)
      {
        bool sent = false;

        for (auto i = 0; i < senders_.size() && msgs_sent < expected_msgs; ++i)
        {
          auto& sender = senders_[conn_idx++];

          if (conn_idx == senders_.size()) { conn_idx = 0; }

          if (sender.is_buffer_full()) { continue; }

          sender.send([&, this](void* buf, std::size_t size) {
            std::uint64_t const now = utility::nanos_since_epoch();
            std::memcpy(buf, &now, sizeof(now));
            // std::memset(buffer, 'a' + (buffer_id_head_ % 26), size);
            return size; // msgs_sent % 1000 + 1; // Return the actual size written
          });

          ++msgs_sent;
          sent = true;
        }

        if (!sent) { break; }
        // Use the sender to send data
      }

      total_msgs_sent_.store(msgs_sent);
      io_ctx_.poll();
    }
  }
}