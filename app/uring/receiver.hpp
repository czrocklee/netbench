#pragma once

#include "utility/metric_hud.hpp"
#include "connection.hpp"
#include "uring/io_context.hpp"
#include "uring/provided_buffer_pool.hpp"

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <thread>

namespace uring
{

  class receiver
  {
  public:
    struct config
    {
      unsigned uring_depth;
      unsigned buffer_count;
      unsigned buffer_size;
      uint16_t buffer_group_id;
      io_uring_params params{};
    };

    explicit receiver(config cfg);
    ~receiver();

    receiver(const receiver&) = delete;
    receiver& operator=(const receiver&) = delete;
    receiver(receiver&&) = delete;
    receiver& operator=(receiver&&) = delete;

    void start();
    void stop();
    void add_connection(bsd::socket&& sock);
    bool post(std::move_only_function<void()> task);

    utility::metric_hud::metric get_metrics();

    io_context& get_io_context() { return io_ctx_; }

  private:
    void run();
    void process_pending_tasks();

    config config_;
    std::atomic<bool> stop_flag_{false};
    io_context io_ctx_;
    provided_buffer_pool buffer_pool_;
    std::list<connection> connections_;
    boost::lockfree::spsc_queue<std::move_only_function<void()>> pending_task_queue_;
    std::thread thread_;
  };

} // namespace uring