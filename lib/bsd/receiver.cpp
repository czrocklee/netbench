#include "receiver.hpp"
#include <utility>
#include <cstring>

namespace bsd
{

  receiver::receiver(io_context& io_ctx, std::size_t buffer_size)
    : io_ctx_{io_ctx}
  {
    buffer_.resize(buffer_size);
  }

  void receiver::open(bsd::socket sock)
  {
    sock_ = std::move(sock);
    sock_.set_nonblocking(true);
  }

  void receiver::start(data_callback&& cb)
  {
    data_cb_ = std::move(cb);
    read_evt_ = io_ctx_.register_event(sock_.get_fd(), EPOLLIN | EPOLLERR | EPOLLET, &receiver::on_events, this);
  }

  void receiver::on_events(uint32_t events, void* context)
  {
    auto* self = static_cast<receiver*>(context);
    self->handle_events(events);
  }

  void receiver::handle_events(std::uint32_t events)
  {
    if (events & EPOLLERR)
    {
      int socket_error = 0;
      ::socklen_t optlen = sizeof(socket_error);
      // Get the pending error from the socket
      if (::getsockopt(sock_.get_fd(), SOL_SOCKET, SO_ERROR, &socket_error, &optlen) == 0)
      {
        data_cb_(std::make_error_code(static_cast<std::errc>(socket_error)), {});
      }

      return;
    }

    if (events & EPOLLIN)
    {
      do_read();
    }
  }

  void receiver::do_read()
  {
    std::size_t bytes_read = 0;

    while (true)
    {
      auto n = ::read(sock_.get_fd(), buffer_.data(), buffer_.size());

      if (n > 0)
      {
        data_cb_({}, ::asio::const_buffer{buffer_.data(), static_cast<size_t>(n)});

        if (read_limit_ > 0)
        {
          if ((bytes_read += n) >= read_limit_)
          {
            // Stop reading if the read limit is reached, rearm the socket for future read
            read_evt_.rearm();
            break;
          }
        }
      }
      else if (n == 0)
      {
        data_cb_(::asio::error::make_error_code(::asio::error::eof), {});
        break;
      }
      else
      {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          // No more data to read, exit the loop
          break;
        }

        data_cb_(std::make_error_code(static_cast<std::errc>(errno)), {});
      }
    }
  }

} // namespace bsd
