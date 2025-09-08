#pragma once

#include <utility/tagged_integer.hpp>

#include <liburing.h>
#include <cstdint>
#include <chrono>
#include <type_traits>
#include <any>
#include <list>

namespace uring
{
  class registered_buffer_pool;

  class io_context
  {
  public:
    ~io_context();

    explicit io_context(unsigned entries = 1024 * 16);
    io_context(unsigned entries, ::io_uring_params& params);

    void enable();

    void poll();
    void poll_wait();
    template<typename Rep, typename Period>
    void run_for(std::chrono::duration<Rep, Period> const& timeout);
    void wakeup();

    [[nodiscard]] ::io_uring& get_ring() noexcept { return ring_; }

    class file_handle;
    [[nodiscard]] file_handle create_fixed_file(int fd);

    void init_buffer_pool(std::size_t buf_size, std::uint16_t buf_cnt);
    [[nodiscard]] registered_buffer_pool& get_buffer_pool() noexcept { return *buf_pool_; }

    class request_handle;
    using completion_handler_type = void (*)(::io_uring_cqe const&, void* context);
    [[nodiscard]] ::io_uring_sqe&
      create_request(request_handle& handle, completion_handler_type handler, void* context);

    using prepare_handler_type = void (*)(::io_uring_sqe&, void* context);
    void prepare_request(request_handle& handle, prepare_handler_type handler, void* context);

  private:
    struct req_data
    {
      prepare_handler_type prepare_handler = nullptr;
      void* prepare_context = nullptr;
      completion_handler_type completion_handler = nullptr;
      void* completion_context = nullptr;
    };

    void init(unsigned entries, ::io_uring_params& params);
    static void on_wakeup(::io_uring_cqe const& cqe, void* context);
    void rearm_wakeup_event();
    void run_for_impl(__kernel_timespec const* ts);
    void process_cqe(::io_uring_cqe* cqe);
    request_handle new_request(req_data data);
    void free_request(request_handle& handle);
    void finish_preparing_requests();

    ::io_uring ring_;

    using data_node_iter = std::list<req_data>::iterator;
    std::list<req_data> free_data_list_;
    std::list<req_data> active_data_list_;
    std::vector<req_data*> preparing_data_list_;

    int wakeup_fd_ = -1;
    std::uint64_t wakeup_buffer_;
    std::unique_ptr<request_handle> wakeup_handle_;

    std::any fixed_file_table_;
    std::unique_ptr<registered_buffer_pool> buf_pool_;

    friend class request_handle;
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

    void set_completion_handler(completion_handler_type handler, void* context) noexcept;
    bool is_valid() const noexcept { return io_ctx_ != nullptr; }

  private:
    request_handle(io_context& io_ctx, data_node_iter data_iter);
    io_context* io_ctx_ = nullptr;
    io_context::data_node_iter data_iter_;
    friend class io_context;
  };

  class io_context::file_handle
  {
  public:
    file_handle() = default;
    explicit file_handle(int fd);
    ~file_handle();
    file_handle(file_handle const&) = delete;
    file_handle& operator=(file_handle const&) = delete;
    file_handle(file_handle&& other) noexcept;
    file_handle& operator=(file_handle&& other) noexcept;

    int get_fd() const noexcept { return fd_; }
    bool is_valid() const noexcept { return fd_ != -1; }
    bool has_fixed() const noexcept { return io_ctx_ != nullptr; }
    void update_sqe_flag(::io_uring_sqe& sqe) const noexcept;

  private:
    file_handle(int fd, io_context* io_ctx);
    int fd_ = -1;
    io_context* io_ctx_ = nullptr;
    friend class io_context;
  };
}