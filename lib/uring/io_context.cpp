#include "io_context.hpp"
#include "registered_buffer_pool.hpp"

#include <string.h>
#include <sys/eventfd.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <iostream>
#include <boost/icl/interval_set.hpp>

namespace uring
{
  io_context::io_context(unsigned entries)
  {
    ::io_uring_params params{};
    init(entries, params);
  }

  io_context::io_context(unsigned entries, ::io_uring_params& params)
  {
    init(entries, params);
  }

  io_context::~io_context()
  {
    if (wakeup_fd_ != -1) { ::close(wakeup_fd_); }

    io_uring_queue_exit(&ring_);
  }

  void io_context::enable()
  {
    ::io_uring_enable_rings(&ring_);
  }

  void io_context::poll()
  {

    // if (int ret = io_uring_submit(&ring_); ret < 0)
    if (int ret = io_uring_submit(&ring_); ret < 0)
    {
      // EINTR is a recoverable error, we can just try again on the next poll.
      if (-ret == EINTR) return;

      throw std::system_error{-ret, std::system_category(), "io_uring_submit failed"};
    }

    ++sub_seq_;
    ::io_uring_cqe* cqes[16];

    auto count = ::io_uring_peek_batch_cqe(&ring_, cqes, 16);

    if (count == 0) { return; }

    for (unsigned i = 0; i < count; ++i) { process_cqe(cqes[i]); }

    ::io_uring_cq_advance(&ring_, count);
  }

  void io_context::poll_wait()
  {

    if (int ret = io_uring_submit_and_wait(&ring_, 1); ret < 0)
    {
      // EINTR is a recoverable error, we can just try again on the next poll.
      if (-ret == EINTR) return;

      throw std::system_error{-ret, std::system_category(), "io_uring_submit failed"};
    }

    ++sub_seq_;
    ::io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe)
    {
      process_cqe(cqe);
      ++count;
    }

    ::io_uring_cq_advance(&ring_, count);
  }

  io_context::file_handle io_context::create_fixed_file(int fd)
  {
    constexpr static unsigned max_fixed_file_array_size = 1024 * 4;

    class fixed_file_table
    {
    public:
      fixed_file_table(io_context& ctx) : ctx_{ctx}
      {
        if (int ret = ::io_uring_register_files_sparse(&ctx_.get_ring(), max_fixed_file_array_size); ret < 0)
        {
          throw std::system_error{-ret, std::system_category(), "io_uring_register_files_sparse failed"};
        }
      }

      ~fixed_file_table() { ::io_uring_unregister_files(&ctx_.get_ring()); }

    private:
      io_context& ctx_;
    };

    if (fd < 0) { throw std::invalid_argument("Invalid file descriptor for file_handle"); }

    if (!fixed_file_table_.has_value()) { fixed_file_table_.emplace<fixed_file_table>(*this); }

    return file_handle{fd, fd < max_fixed_file_array_size ? this : nullptr};
  }

  void io_context::init_buffer_pool(std::uint16_t buf_cnt, std::size_t buf_size)
  {
    if (buf_pool_) { throw std::runtime_error("Buffer pool is already initialized"); }

    buf_pool_ = std::make_unique<registered_buffer_pool>(*this, buf_cnt, buf_size);
  }
  /*
    io_context::request_handle io_context::create_request(handler_type handler, void* context)
    {
      ::io_uring_sqe* sqe = nullptr;

      while ((sqe = io_uring_get_sqe(&ring_)) == nullptr)
      {
        io_uring_submit(&ring_);
        ++sub_seq_;
      }

      std::size_t index = (sqe - ring_.sq.sqes);
      req_data_array_[index] = req_data{.handler = handler, .context = context};
      ::io_uring_sqe_set_data(sqe, &req_data_array_[index]);

      return request_handle{sqe, &req_data_array_[index]};
    }

    io_context::request_handle io_context::create_request(file_handle const& file, handler_type handler, void* context)
    {
      auto handle = create_request(handler, context);
      auto& sqe = handle.get_sqe();
      sqe.fd = file.get_fd();

      if (file.has_fixed()) { sqe.flags |= IOSQE_FIXED_FILE; }

      return handle;
    } */

  ::io_uring_sqe& io_context::create_request(request_handle& handle, handler_type handler, void* context)
  {
    ::io_uring_sqe* sqe = nullptr;

    while ((sqe = io_uring_get_sqe(&ring_)) == nullptr)
    {
      io_uring_submit(&ring_);
      ++sub_seq_;
    }

    if (!handle.is_valid() || handle.io_ctx_ != this)
    {
      handle = new_request(handler, context);
    }
    else
    {
      *handle.data_iter_ = req_data{.handler = handler, .context = context};
    }

    ::io_uring_sqe_set_data(sqe, std::addressof(*handle.data_iter_));

    return *sqe;
  }

  ::io_uring_sqe&
  io_context::create_request(request_handle& handle, file_handle const& file, handler_type handler, void* context)
  {
    auto& sqe = create_request(handle, handler, context);
    sqe.fd = file.get_fd();

    if (file.has_fixed()) { sqe.flags |= IOSQE_FIXED_FILE; }

    return sqe;
  }

  io_context::request_handle io_context::new_request(handler_type handler, void* context)
  {
    data_node_iter iter;

    if (!free_data_list_.empty())
    {
      active_data_list_.splice(active_data_list_.begin(), free_data_list_, free_data_list_.begin());
      iter = active_data_list_.begin();
      *iter = req_data{.handler = handler, .context = context};
    }
    else
    {
      iter = active_data_list_.emplace(active_data_list_.begin(), req_data{.handler = handler, .context = context});
    }

    return request_handle{*this, iter};
  }

  void io_context::free_request(request_handle& handle)
  {
    handle.data_iter_->handler = nullptr;
    free_data_list_.splice(free_data_list_.begin(), active_data_list_, handle.data_iter_);
  }

  void io_context::init(unsigned entries, ::io_uring_params& params)
  {
    if (int ret = io_uring_queue_init_params(entries, &ring_, &params); ret < 0)
    {
      throw std::system_error{-ret, std::system_category(), "io_uring_queue_init_params failed"};
    }

    req_data_array_ = std::make_unique<req_data[]>(params.sq_entries);

    if (wakeup_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); wakeup_fd_ < 0)
    {
      throw std::system_error{errno, std::system_category(), "eventfd creation failed"};
    }

    rearm_wakeup_event();
  }

  void io_context::run_for_impl(__kernel_timespec const* ts)
  {
    ::io_uring_cqe* cqe;
    int ret = ::io_uring_submit_and_wait_timeout(&ring_, &cqe, 1, const_cast<__kernel_timespec*>(ts), nullptr);

    if (ret < 0)
    {
      // ETIME means the timeout was reached, which is not an error for run_for.
      // We still proceed to process any completions that might have occurred.
      // EINTR is also a recoverable error we can ignore.
      if (-ret != ETIME && -ret != EINTR)
      {
        throw std::system_error{-ret, std::system_category(), "io_uring_submit_and_wait_timeout failed"};
      }
    }

    // Process any completions that are ready.
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe)
    {
      process_cqe(cqe);
      ++count;
    }

    ::io_uring_cq_advance(&ring_, count);
  }

  void io_context::rearm_wakeup_event()
  {
    wakeup_handle_ = std::make_unique<request_handle>();
    auto& sqe = create_request(*wakeup_handle_, on_wakeup, this);
    ::io_uring_prep_read(&sqe, wakeup_fd_, &wakeup_buffer_, sizeof(wakeup_buffer_), 0);
  }

  void io_context::on_wakeup(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<io_context*>(context);

    if (cqe.res < 0)
    {
      // This is a critical error, likely during shutdown.
      fprintf(stderr, "Wakeup eventfd read error: %s\n", strerror(-cqe.res));
      return;
    }
    // The read is complete, so we re-arm it for the next wakeup call.
    self.rearm_wakeup_event();
  }

  void io_context::wakeup()
  {
    if (std::uint64_t val = 1; ::write(wakeup_fd_, &val, sizeof(val)) < 0) { perror("write to eventfd failed"); }
  }

  void io_context::process_cqe(::io_uring_cqe* cqe)
  {
    auto* data = reinterpret_cast<req_data*>(::io_uring_cqe_get_data(cqe));

    if (data->handler != nullptr) { data->handler(*cqe, data->context); }
  }

  io_context::request_handle::request_handle(io_context& io_ctx, data_node_iter data_iter)
    : io_ctx_{&io_ctx}, data_iter_{data_iter}
  {
  }

  io_context::request_handle::~request_handle()
  {
    if (io_ctx_ != nullptr) { io_ctx_->free_request(*this); }
  }

  io_context::request_handle::request_handle(request_handle&& other) noexcept
    : io_ctx_{other.io_ctx_}, data_iter_{other.data_iter_}
  {
    other.io_ctx_ = nullptr;
    other.data_iter_ = {};
  }

  io_context::request_handle& io_context::request_handle::operator=(request_handle&& other) noexcept
  {
    if (this != &other)
    {
      if (io_ctx_ != nullptr) { io_ctx_->free_request(*this); }

      io_ctx_ = other.io_ctx_;
      data_iter_ = other.data_iter_;
      other.io_ctx_ = nullptr;
      other.data_iter_ = {};
    }

    return *this;
  }

  io_context::file_handle::file_handle(int fd) : file_handle{fd, nullptr}
  {
  }

  io_context::file_handle::file_handle(int fd, io_context* io_ctx) : fd_{fd}, io_ctx_{io_ctx}
  {
    if (fd_ >= 0 && io_ctx_ != nullptr)
    {
      int set[] = {fd_};

      if (::io_uring_register_files_update(&io_ctx_->get_ring(), static_cast<unsigned int>(fd_), set, 1) < 0)
      {
        io_ctx_ = nullptr; // fail silently
      }
    }
  }

  io_context::file_handle::~file_handle()
  {
    if (fd_ >= 0 && io_ctx_ != nullptr)
    {
      int set[] = {-1};
      ::io_uring_register_files_update(&io_ctx_->get_ring(), static_cast<unsigned int>(fd_), set, 1);
    }
  }

  io_context::file_handle::file_handle(file_handle&& other) noexcept : fd_{other.fd_}, io_ctx_{other.io_ctx_}
  {
    other.fd_ = -1;
    other.io_ctx_ = nullptr;
  }

  io_context::file_handle& io_context::file_handle::operator=(file_handle&& other) noexcept
  {
    if (this != &other)
    {
      fd_ = other.fd_;
      io_ctx_ = other.io_ctx_;
      other.fd_ = -1;
      other.io_ctx_ = nullptr;
    }

    return *this;
  }
}
