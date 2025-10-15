#pragma once

#include "bsd/socket.hpp"
#include "metric_hud.hpp"

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
#include <chrono>
#include <optional>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <thread>

class worker
{
public:
  struct config
  {
    enum class echo_mode
    {
      none = 0,
      per_op,
      per_msg
    };

    echo_mode echo;
    std::size_t buffer_size;
    int socket_recv_buffer_size;
    int socket_send_buffer_size;
    int collect_latency_every_n_samples;
    bool shutdown_on_disconnect = false;
    std::atomic<int>* shutdown_counter = nullptr;
#ifdef IO_URING_API
    bool zerocopy;
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

  void start(bool busy_spin, int cpu_id = -1);
  void stop();
  void add_connection(net::socket sock);
  bool post(std::move_only_function<void()> task);

  metric const& get_metrics() const { return metrics_; }
  net::io_context& get_io_context() { return io_ctx_; }

private:
  struct connection
  {
    template<typename... Args>
    connection(Args&&... args) : receiver{std::forward<Args>(args)...}
    {
    }

    net::receiver receiver;
    std::optional<net::sender> sender;
    std::size_t msg_size;
    std::unique_ptr<std::byte[]> partial_buffer;
    std::size_t partial_buffer_size = 0;
  };

  void run();
  void run_busy_spin();

  void process_pending_tasks();
  void on_data(connection& conn, ::asio::const_buffer const data);
  void on_new_message(connection& conn, void const* buffer);

  config config_;
  std::atomic<bool> stop_flag_{false};
  net::io_context io_ctx_;
#ifdef IO_URING_API
  uring::provided_buffer_pool recv_pool_;
#endif
  metric metrics_{};
  std::list<connection> connections_;
  boost::lockfree::spsc_queue<std::move_only_function<void()>> pending_task_queue_;
  std::thread thread_;
};
