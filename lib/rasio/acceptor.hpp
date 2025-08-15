#pragma once

#include "handler_allocator.hpp"
#include "socket.hpp"

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <functional>
#include <system_error>
#include <string>

namespace rasio
{
  class acceptor
  {
  public:
    using accept_callback = std::function<void(std::error_code, socket)>;

    explicit acceptor(::asio::io_context& io_ctx);

    void listen(std::string const& address, std::string const& port);

    void start(accept_callback cb);

  private:
    void do_accept();

    ::asio::basic_socket_acceptor<::asio::ip::tcp, ::asio::io_context::executor_type> acceptor_;
    dynamic_handler_memory handler_memory_;
    accept_callback accept_cb_;
  };
} // namespace rasio
