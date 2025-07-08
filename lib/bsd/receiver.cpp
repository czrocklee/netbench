#include "receiver.hpp"
#include <utility>
#include <cstring>

namespace bsd
{

  receiver::receiver(io_context& io_ctx, std::size_t buffer_size)
    : io_ctx_{io_ctx}, event_data_{&receiver::on_events, this}
  {
    buffer_.resize(buffer_size);
  }

  void receiver::open(bsd::socket sock) { sock_ = std::move(sock); }

  void receiver::start(data_callback&& cb)
  {
    data_cb_ = std::move(cb);
    io_ctx_.add(sock_.get_fd(), EPOLLIN | EPOLLERR | EPOLLET, &event_data_);
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

    while (true)
    {
      auto n = ::read(sock_.get_fd(), buffer_.data(), buffer_.size());

      if (n > 0)
      {
        data_cb_({}, ::asio::const_buffer{buffer_.data(), static_cast<size_t>(n)});
      }
      else if (n == 0)
      {
        data_cb_(::asio::error::make_error_code(::asio::error::eof), {});
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
