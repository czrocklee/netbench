#include "io_context.hpp"

#include <string.h>
#include <stdexcept>
#include <system_error>

namespace uring
{

  io_context::io_context(unsigned entries)
  {
    if (int ret = io_uring_queue_init(entries, &ring_, 0); ret < 0)
    {
      throw std::system_error{-ret, std::system_category(), "io_uring_queue_init failed"};
    }
  }

  io_context::io_context(unsigned entries, ::io_uring_params& params)
  {
    if (int ret = io_uring_queue_init_params(entries, &ring_, &params); ret < 0)
    {
      throw std::system_error{-ret, std::system_category(), "io_uring_queue_init_params failed"};
    }
  }

  io_context::~io_context() { io_uring_queue_exit(&ring_); }

  void io_context::poll()
  {
    int ret = io_uring_submit_and_wait(&ring_, 1);
    if (ret < 0)
    {
      // EINTR is a recoverable error, for example, when the process is being
      // traced. We can just continue the event loop.
      if (-ret == EINTR) return;
      throw std::system_error{-ret, std::system_category(), "io_uring_submit_and_wait failed"};
    }

    ::io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe)
    {
      count++;
      auto* data = reinterpret_cast<req_data*>(::io_uring_cqe_get_data(cqe));
      if (data && data->handler)
      {
        data->handler(*cqe, data->context);
      }
    }

    ::io_uring_cq_advance(&ring_, count);
  }

  void io_context::wakeup()
  {
    // This function is designed to be called from another thread to wake up
    // a call to poll() that might be blocked in io_uring_submit_and_wait.
    // Submitting a no-op is a standard way to achieve this.
    ::io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) { return; } // Not much we can do if the queue is full. poll() will unblock eventually.
    ::io_uring_prep_nop(sqe);
    ::io_uring_sqe_set_data(sqe, nullptr); // No handler, this is just for waking up.
    ::io_uring_submit(&ring_);
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
