#pragma once

#include <liburing.h>
#include <cstdint>
#include <chrono>
#include <type_traits>

namespace uring
{
  class io_context
  {
  public:
    ~io_context();

    explicit io_context(unsigned entries);

    io_context(unsigned entries, ::io_uring_params& params);

    void poll();

    template<typename Rep, typename Period>
    void run_for(const std::chrono::duration<Rep, Period>& timeout);

    void wakeup();

    int get_ring_fd() const { return ring_.ring_fd; }

  private:
    using handler_type = std::add_pointer<void(const ::io_uring_cqe&, void* context)>::type;

    struct req_data
    {
      handler_type handler;
      void* context;
    };

    void setup_wakeup_event();
    static void on_wakeup(const ::io_uring_cqe& cqe, void* context);
    void rearm_wakeup_event();

    void run_for_impl(const __kernel_timespec* ts);

    ::io_uring_sqe& create_request(req_data& data);

    void process_cqe(::io_uring_cqe* cqe, unsigned& count);

    ::io_uring* get_ring() { return &ring_; }



    ::io_uring ring_;
    int wakeup_fd_ = -1;
    std::uint64_t wakeup_buffer_{};
    req_data wakeup_req_data_{};

    friend class connection;
    friend class acceptor;
    friend class provided_buffer_pool;
  };

  template<typename Rep, typename Period>
  void io_context::run_for(const std::chrono::duration<Rep, Period>& timeout)
  {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
    ::__kernel_timespec ts = {
      .tv_sec = static_cast<__kernel_time_t>(ns.count() / 1000000000),
      .tv_nsec = static_cast<long>(ns.count() % 1000000000)};
    run_for_impl(&ts);
  }
}