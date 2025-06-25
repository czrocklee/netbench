#pragma once

#include "../bsd/socket.hpp"
#include "io_context.hpp"
#include <functional>
#include <string>
#include <utility>

namespace bsd
{
  class acceptor
  {
  public:
    using on_accept_callback = std::function<void(bsd::socket&&)>;

    explicit acceptor(io_context& io_ctx, on_accept_callback on_accept_cb);

    void listen(const std::string& address, const std::string& port, int backlog = SOMAXCONN);
    void start();

  private:
    static void on_events(uint32_t events, void* context);
    void handle_events(uint32_t events);

    io_context& io_ctx_;
    bsd::socket listen_sock_;
    on_accept_callback on_accept_cb_;
    io_context::event_data event_data_;
  };
}