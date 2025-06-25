#pragma once

#include <liburing.h>
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

    void wakeup();

  private:
    friend class provided_buffer_pool; // Allow access to ring_

    using handler_type = std::add_pointer<void(const ::io_uring_cqe&, void* context)>::type;

    struct req_data
    {
      handler_type handler;
      void* context;
    };

    ::io_uring_sqe& create_request(req_data& data);

    ::io_uring* get_ring() { return &ring_; }

    // std::unique_ptr<req_data[]> req_data_bucket_;
    // std::stack<req_data*, std::vector<req_data>> free_req_data_;
    ::io_uring ring_;

    friend class connection;
    friend class acceptor;
    friend class provided_buffer_pool;
  };
}
