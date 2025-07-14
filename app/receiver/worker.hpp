#pragma once

#include "bsd/socket.hpp"
#include "utility/metric_hud.hpp"

#ifdef IO_URING_API
#include "uring/tcp.hpp"
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
#ifdef IO_URING_API
    std::uint32_t uring_depth;
    std::uint16_t buffer_count;
    std::uint16_t buffer_group_id;
    ::io_uring_params params{};
#elifdef BSD_API
    unsigned read_limit = 0;
#endif 
  };

  explicit worker(config cfg);
  ~worker();

  worker(worker const&) = delete;
  worker& operator=(worker const&) = delete;
  worker(worker&&) = delete;
  worker& operator=(worker&&) = delete;

  void start();
  void stop();
  void add_connection(net::socket sock);
  bool post(std::move_only_function<void()> task);

  utility::metric_hud::metric get_metrics() const { return metrics_; }

  net::io_context& get_io_context() { return io_ctx_; }

private:
  void run();
  void process_pending_tasks();

  config config_;
  std::atomic<bool> stop_flag_{false};
  net::io_context io_ctx_;
#ifdef IO_URING_API
  uring::provided_buffer_pool buffer_pool_;
#elifdef ASIO_API
  asio::executor_work_guard<::asio::io_context::executor_type> work_guard_;
#endif
  utility::metric_hud::metric metrics_{};
  std::list<net::receiver> connections_;
  boost::lockfree::spsc_queue<std::move_only_function<void()>> pending_task_queue_;
  std::thread thread_;
};
