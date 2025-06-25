#include "connection.hpp"
#include <iostream>
#include <unistd.h>
#include <utility>

namespace bsd
{
  connection::connection(io_context& io_ctx, size_t buffer_size)
    : io_ctx_(io_ctx), buffer_(buffer_size), is_closed_{false}
  {
    event_data_.handler = &connection::on_events;
    event_data_.context = this;
  }

  connection::~connection()
  {
    if (!is_closed_ && sock_.get_fd() != -1)
    {
      io_ctx_.remove(sock_.get_fd());
    }
  }

  void connection::open(bsd::socket&& sock) { sock_ = std::move(sock); }

  void connection::start()
  {
    // Register with epoll for read events, using Edge-Triggered mode.
    io_ctx_.add(sock_.get_fd(), EPOLLIN | EPOLLET, &event_data_);
  }

  void connection::on_events(uint32_t events, void* context)
  {
    auto* self = static_cast<connection*>(context);
    self->handle_events(events);
  }

  void connection::handle_events(uint32_t events)
  {
    // Check for errors first.
    if ((events & EPOLLERR) || (events & EPOLLHUP))
    {
      fprintf(stderr, "epoll error on fd %d\n", sock_.get_fd());
      is_closed_ = true;
      return;
    }

    if (events & EPOLLIN)
    {
      do_read();
    }
  }

  void connection::do_read()
  {
    while (true)
    {
      ssize_t bytes_read = ::read(sock_.get_fd(), buffer_.data(), buffer_.size());
      if (bytes_read > 0)
      {
        metrics_.bytes += bytes_read;
        metrics_.msgs++;
      }
      else if (bytes_read == 0)
      {
        // Client disconnected gracefully.
        printf("Client disconnected, fd: %d\n", sock_.get_fd());
        is_closed_ = true;
        break;
      }
      else // bytes_read == -1
      {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          // We have read all available data for now.
          break;
        }
        perror("read error");
        is_closed_ = true;
        break;
      }
    }
  }
}