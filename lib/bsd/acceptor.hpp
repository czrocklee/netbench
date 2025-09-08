#pragma once

#include "socket.hpp"
#include "io_context.hpp"
#include <functional>
#include <string>
#include <utility>

namespace bsd
{
  class acceptor
  {
  public:
    using accept_callback = std::move_only_function<void(std::error_code, bsd::socket)>;

    explicit acceptor(io_context& io_ctx);

    void listen(std::string const& address, std::string const& port, int backlog = SOMAXCONN);

    void start(accept_callback accept_cb);

  private:
    static void on_events(std::uint32_t events, void* context);
    void handle_accept();

    io_context& io_ctx_;
    socket listen_sock_;
    accept_callback accept_cb_;
    io_context::event_handle accept_evt_;
  };
}