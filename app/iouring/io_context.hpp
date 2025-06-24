#pragma once

#include <liburing/io_uring.h>

class io_context
{
public:
  explicit io_context(unsigned entries);

  io_context(unsigned entries, ::io_uring_params& params);

  void poll();

private:
  using handler_type = void(const ::io_uring_cqe&);

  struct req_data
  {
    handler_type hander;
    void* context;
  };

  ::io_uring_sqe& create_request(const req_data&);
 
  //std::unique_ptr<req_data[]> req_data_bucket_;
  //std::stack<req_data*, std::vector<req_data>> free_req_data_;
  ::io_uring ring_;
};
