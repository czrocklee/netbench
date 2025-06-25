#include "acceptor.hpp"
#include <fcntl.h>
#include <utility>

namespace
{
  // Helper to set a socket to non-blocking mode.
  void make_socket_non_blocking(int sfd)
  {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) { throw bsd::socket_exception("fcntl F_GETFL"); }
    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) == -1) { throw bsd::socket_exception("fcntl F_SETFL"); }
  }
}

namespace bsd
{
  acceptor::acceptor(io_context& io_ctx, on_accept_callback on_accept_cb)
    : io_ctx_(io_ctx), on_accept_cb_(std::move(on_accept_cb))
  {
    event_data_.handler = &acceptor::on_events;
    event_data_.context = this;
  }

  void acceptor::listen(const std::string& address, const std::string& port, int backlog)
  {
    listen_sock_ = bsd::socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    listen_sock_.set_option(SOL_SOCKET, SO_REUSEADDR, optval);
    listen_sock_.bind(address, port);
    make_socket_non_blocking(listen_sock_.get_fd());
    listen_sock_.listen(backlog);
  }

  void acceptor::start() { io_ctx_.add(listen_sock_.get_fd(), EPOLLIN | EPOLLET, &event_data_); }

  void acceptor::on_events(uint32_t events, void* context)
  {
    auto* self = static_cast<acceptor*>(context);
    self->handle_events(events);
  }

  void acceptor::handle_events(uint32_t events)
  {
    if (events & EPOLLIN)
    {
      // Since we use Edge-Triggered (EPOLLET), we must accept all pending connections.
      while (true)
      {
        try
        {
          bsd::socket new_sock = listen_sock_.accept();
          make_socket_non_blocking(new_sock.get_fd());
          if (on_accept_cb_)
          {
            on_accept_cb_(std::move(new_sock));
          }
        }
        catch (const bsd::socket_exception& e)
        {
          if (e.code().value() == EAGAIN || e.code().value() == EWOULDBLOCK)
          {
            // All pending connections have been accepted.
            break;
          }
          perror("accept error");
          break;
        }
      }
    }
  }
}