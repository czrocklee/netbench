#include "io_context.hpp"

#include <string.h>
#include <sys/eventfd.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace uring
{

  io_context::io_context(unsigned entries)
  {
    if (int ret = io_uring_queue_init(entries, &ring_, 0); ret < 0)
    {
      throw std::system_error{-ret, std::system_category(), "io_uring_queue_init failed"};
    }

    setup_wakeup_event();
  }

  io_context::io_context(unsigned entries, ::io_uring_params& params)
  {
    if (int ret = io_uring_queue_init_params(entries, &ring_, &params); ret < 0)
    {
      throw std::system_error{-ret, std::system_category(), "io_uring_queue_init_params failed"};
    }

    setup_wakeup_event();
  }

  io_context::~io_context()
  {
    if (wakeup_fd_ != -1)
    {
      ::close(wakeup_fd_);
    }

    io_uring_queue_exit(&ring_);
  }

  void io_context::poll()
  {
    if (int ret = io_uring_submit_and_wait(&ring_, 1); ret < 0)
    {
      // EINTR is a recoverable error, we can just try again on the next poll.
      if (-ret == EINTR)
        return;

      throw std::system_error{-ret, std::system_category(), "io_uring_submit failed"};
    }

    // Peek the completion queue for any events that are ready.
    ::io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe) { process_cqe(cqe, count); }

    ::io_uring_cq_advance(&ring_, count);
  }

  void io_context::run_for_impl(const __kernel_timespec* ts)
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

    io_uring_for_each_cqe(&ring_, head, cqe) { process_cqe(cqe, count); }

    ::io_uring_cq_advance(&ring_, count);
  }

  void io_context::setup_wakeup_event()
  {
    wakeup_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (wakeup_fd_ < 0)
    {
      throw std::system_error{errno, std::system_category(), "eventfd creation failed"};
    }

    wakeup_req_data_ = {on_wakeup, this};
    rearm_wakeup_event();
  }

  void io_context::rearm_wakeup_event()
  {
    auto& sqe = create_request(wakeup_req_data_);
    ::io_uring_prep_read(&sqe, wakeup_fd_, &wakeup_buffer_, sizeof(wakeup_buffer_), 0);
  }

  void io_context::on_wakeup(const ::io_uring_cqe& cqe, void* context)
  {
    auto* self = static_cast<io_context*>(context);
    if (cqe.res < 0)
    {
      // This is a critical error, likely during shutdown.
      fprintf(stderr, "Wakeup eventfd read error: %s\n", strerror(-cqe.res));
      return;
    }
    // The read is complete, so we re-arm it for the next wakeup call.
    self->rearm_wakeup_event();
  }

  void io_context::wakeup()
  {
    uint64_t val = 1;
    if (::write(wakeup_fd_, &val, sizeof(val)) < 0)
    {
      perror("write to eventfd failed");
    }
  }

  void io_context::process_cqe(::io_uring_cqe* cqe, unsigned& count)
  {
    ++count;
    auto* data = reinterpret_cast<req_data*>(::io_uring_cqe_get_data(cqe));
    if (data && data->handler)
    {
      data->handler(*cqe, data->context);
    }
  }

  ::io_uring_sqe& io_context::create_request(req_data& data)
  {
    ::io_uring_sqe* sqe = io_uring_get_sqe(&ring_);

    if (!sqe)
    {
      throw std::runtime_error("io_uring submission queue is full");
    }
    // It's good practice to clear the SQE before use.
    ::io_uring_prep_nop(sqe);
    ::io_uring_sqe_set_data(sqe, &data);
    return *sqe;
  }

}
