#include "acceptor.hpp"
#include <fcntl.h>
#include <utility>
#include <iostream>

namespace bsd
{
  acceptor::acceptor(io_context& io_ctx) : io_ctx_{io_ctx}
  {
  }

  void acceptor::listen(std::string const& address, std::string const& port, int backlog)
  {
    listen_sock_ = bsd::socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    listen_sock_.set_option(SOL_SOCKET, SO_REUSEADDR, optval);
    listen_sock_.bind(address, port);
    listen_sock_.set_nonblocking(true);
    listen_sock_.listen(backlog);
  }

  void acceptor::start(accept_callback accept_cb)
  {
    accept_cb_ = std::move(accept_cb);
    accept_evt_ =
      io_ctx_.register_event(listen_sock_.get_fd(), EPOLLIN | EPOLLERR | EPOLLET, &acceptor::on_events, this);
  }

  void acceptor::on_events(uint32_t events, void* context)
  {
    auto& self = *static_cast<acceptor*>(context);

    if (events & EPOLLIN)
    {
      self.handle_accept();
    }
  }

  void acceptor::handle_accept()
  {
    while (true)
    {
      try
      {
        accept_cb_({}, listen_sock_.accept());
      }
      catch (bsd::socket_exception const& e)
      {
        if (e.code().value() != EAGAIN && e.code().value() != EWOULDBLOCK)
        {
          accept_cb_(std::make_error_code(static_cast<std::errc>(e.code().value())), bsd::socket{});
        }

        break;
      }
    }
  }
}