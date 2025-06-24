
#include "io_context.hpp"

#include <string.h>
#include <stdexcept>

void io_context::poll()
{
  if (int ret = io_uring_submit_and_wait(&ring_, 1); ret < 0)
  {
    throw std::runtime_error{std::string{"io_uring_submit_and_wait error: "}.append(::strerror(-ret))};
  }

  ::io_uring_cqe* cqe;
  unsigned head;
  unsigned count = 0;

  io_uring_for_each_cqe(&ring, head, cqe)
  {
    auto* handler = reinterpret_cast<handler_type>(::io_uring_cqe_get_data(cqe)); 
    handle_completion(cqe, &ring, buffers, br, listen_fd);
    ++count;
  }

  ::io_uring_cq_advance(ring_, count);
}

::io_uring_sqe:: io_context::create_request(const& req_data)
{
  

}
