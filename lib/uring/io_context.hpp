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

    explicit io_context(unsigned entries = 1024);
    io_context(unsigned entries, ::io_uring_params& params);

    void enable();

    void poll();
    void poll_wait();
    template<typename Rep, typename Period>
    void run_for(std::chrono::duration<Rep, Period> const& timeout);
    void wakeup();

    ::io_uring& get_ring() noexcept { return ring_; }

    class fixed_file_handle;
    fixed_file_handle create_fixed_file(int fd);

    class request_handle;
    using handler_type = void (*)(::io_uring_cqe const&, void* context);
    request_handle create_request(handler_type handler, void* context);
    request_handle create_request(fixed_file_handle const& file, handler_type handler, void* context);

  private:
    void init(unsigned entries, ::io_uring_params& params);
    static void on_wakeup(::io_uring_cqe const& cqe, void* context);
    void rearm_wakeup_event();
    void run_for_impl(__kernel_timespec const* ts);
    void process_cqe(::io_uring_cqe* cqe);

    struct req_data
    {
      handler_type handler;
      void* context;
    };

    constexpr static unsigned max_fixed_file_array_size = 1024 * 64;

    ::io_uring ring_;
    int wakeup_fd_ = -1;
    std::uint64_t wakeup_buffer_;
    std::unique_ptr<request_handle> wakeup_handle_;
    std::unique_ptr<req_data[]> req_data_array_;

    friend class connection;
    friend class acceptor;
    friend class provided_buffer_pool;
    friend class sender;
  };

  template<typename Rep, typename Period>
  void io_context::run_for(std::chrono::duration<Rep, Period> const& timeout)
  {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
    ::__kernel_timespec ts = {
      .tv_sec = static_cast<__kernel_time_t>(ns.count() / 1000000000),
      .tv_nsec = static_cast<long>(ns.count() % 1000000000)};
    run_for_impl(&ts);
  }

  class io_context::request_handle
  {
  public:
    request_handle() = default;
    ~request_handle();
    request_handle(request_handle const&) = delete;
    request_handle& operator=(request_handle const&) = delete;
    request_handle(request_handle&& other) noexcept;
    request_handle& operator=(request_handle&& other) noexcept;

    ::io_uring_sqe& get_sqe() noexcept { return *sqe_; }

  private:
    request_handle(::io_uring_sqe* sqe, io_context::req_data* data);
    ::io_uring_sqe* sqe_ = nullptr;
    io_context::req_data* data_ = nullptr;
    friend class io_context;
  };

  class io_context::fixed_file_handle
  {
  public:
    fixed_file_handle() = default;
    ~fixed_file_handle();
    fixed_file_handle(fixed_file_handle const&) = delete;
    fixed_file_handle& operator=(fixed_file_handle const&) = delete;
    fixed_file_handle(fixed_file_handle&& other) noexcept;
    fixed_file_handle& operator=(fixed_file_handle&& other) noexcept;

    int get_fd() const noexcept { return fd_; }
    bool has_fixed() const noexcept { return io_ctx_ != nullptr; }

  private:
    fixed_file_handle(int fd, io_context* io_ctx);
    int fd_ = -1;
    io_context* io_ctx_ = nullptr;
    friend class io_context;
  };
}