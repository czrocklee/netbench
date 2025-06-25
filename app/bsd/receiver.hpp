#pragma once

#include "../metric_hud.hpp"
#include "connection.hpp"
#include "io_context.hpp"

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <functional>

namespace bsd
{
  class receiver
  {
  public:
    struct config
    {
      unsigned epoll_size;
      unsigned buffer_size;
    };

    explicit receiver(config cfg);
    ~receiver();

    receiver(const receiver&) = delete;
    receiver& operator=(const receiver&) = delete;

    void start();
    void stop();
    bool add_connection(bsd::socket&& sock);
    bool post(std::function<void()> task);

    metric_hud::metric get_metrics();

  private:
    void run();
    void process_new_connections_and_tasks();

    config config_;
    std::atomic<bool> stop_flag_{false};
    io_context io_ctx_;
    std::list<connection> connections_;
    boost::lockfree::spsc_queue<bsd::socket> pending_connections_queue_;
    boost::lockfree::spsc_queue<std::function<void()>> pending_task_queue_;
    std::thread thread_;
  };
}