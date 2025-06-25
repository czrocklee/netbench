#pragma once

#include "connection.hpp"
#include "io_context.hpp"
#include "provided_buffer_pool.hpp"

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
    };

    explicit receiver(config cfg);
    ~receiver();

    receiver(const receiver&) = delete;
    receiver& operator=(const receiver&) = delete;
    receiver(receiver&&) = delete;
    receiver& operator=(receiver&&) = delete;

    void start();
    void stop();
    bool add_connection(bsd::socket&& sock);
    io_context& get_io_context() { return io_ctx_; }

  private:
    void run();
    void process_new_connections();

    const config config_;
    std::atomic<bool> stop_flag_{false};
    io_context io_ctx_;
    provided_buffer_pool buffer_pool_;
    std::list<connection> connections_;
    boost::lockfree::spsc_queue<bsd::socket> pending_connections_queue_;
    std::thread thread_;
  };

} // namespace uring