#pragma once

#include "metric_hud.hpp"

#ifdef IO_URING_API
#include "uring/tcp.hpp"
#include <uring/bundle_sender.hpp>
using net = uring::tcp;
#elifdef ASIO_API
#include "rasio/tcp.hpp"
using net = rasio::tcp;
#else // BSD_API
#include "bsd/tcp.hpp"
using net = bsd::tcp;
#endif

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <thread>

class worker
{
public:
  struct config
  {
    std::size_t buffer_size;
    std::uint64_t warmup_count = 10000;
    std::uint64_t target_msg_rate = 0; // messages per second, 0 = unlimited
#ifdef IO_URING_API
    std::uint32_t uring_depth;
    std::uint16_t buffer_count;
    ::io_uring_params params{};
#endif
  };

  explicit worker(config cfg);

  void send_initial_message();
  void run(std::atomic<int>& shutdown_counter);
  void add_connection(net::socket sock, std::size_t msg_size);

  net::io_context& get_io_context() { return io_ctx_; }
  auto& get_sample_queue() { return sample_queue_; }

private:
  struct connection
  {
    template<typename... Args>
    connection(Args&&... args) : receiver{std::forward<Args>(args)...}
    {
    }

    void send(::asio::const_buffer const data, std::uint64_t const send_ts);

    net::receiver receiver;
    net::sender sender{receiver.get_io_context()};
    std::size_t msg_size = 0;
    std::uint64_t send_ts = 0;
    std::uint64_t msg_cnt = 0;

    // pacing (initiator only): period 0 means disabled
    std::uint64_t pacing_period_ns = 0;
    std::uint64_t next_eligible_send_ns = 0;
  };

  void on_data(connection& conn, ::asio::const_buffer const data);

  config config_;
  net::io_context io_ctx_;
#ifdef IO_URING_API
  uring::provided_buffer_pool buffer_pool_;
#endif
  std::list<connection> connections_;
  std::size_t closed_conns_ = 0;
  boost::lockfree::spsc_queue<sample> sample_queue_;
  std::atomic<int>* shutdown_counter_ = nullptr;
};
