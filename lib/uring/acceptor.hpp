#pragma once

#include "bsd/socket.hpp"
#include "io_context.hpp"

#include <functional>
#include <string>
#include <utility>

namespace uring
{
  class acceptor
  {
  public:
    using accept_callback = std::move_only_function<void(std::error_code, bsd::socket)>;

    explicit acceptor(io_context& io_ctx);

    void listen(std::string const& address, std::string const& port, int backlog = SOMAXCONN);

    void start(accept_callback accept_cb);

  private:
    static void on_multishot_accept(::io_uring_cqe const& cqe, void* context);
    void new_multishot_accept_op();

    io_context& io_ctx_;
    bsd::socket listen_sock_;
    accept_callback accept_cb_;
    io_context::req_data accept_req_data_;
  };
} // namespace uring