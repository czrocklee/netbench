#include "acceptor.hpp"
#include <fcntl.h>
#include <utility>

namespace bsd
{
  acceptor::acceptor(io_context& io_ctx) : io_ctx_{io_ctx}
  {
    event_data_.handler = &acceptor::on_events;
    event_data_.context = this;
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
    io_ctx_.add(listen_sock_.get_fd(), EPOLLIN | EPOLLET, &event_data_);
  }

  void acceptor::on_events(uint32_t events, void* context)
  {
    auto* self = static_cast<acceptor*>(context);
    self->handle_events(events);
  }

  void acceptor::handle_events(uint32_t events)
  {
    if (events & EPOLLIN)
    {
      while (true)
      {
        try
        {
          accept_cb_({}, std::move(listen_sock_.accept()));
        }
        catch (bsd::socket_exception const& e)
        {
          if (e.code().value() == EAGAIN || e.code().value() == EWOULDBLOCK)
          {
            // All pending connections have been accepted.
            break;
          }

          accept_cb_(std::make_error_code(static_cast<std::errc>(e.code().value())), bsd::socket{});
          break;
        }
      }
    }
  }
}