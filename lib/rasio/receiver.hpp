#pragma once

#include "handler_allocator.hpp"
#include "socket.hpp"
#include <asio/error.hpp>
#include <asio/buffer.hpp>
#include <functional>
#include <system_error>
#include <array>
#include <cstddef>

namespace rasio
{
  class receiver
  {
  public:
    using data_callback = std::function<void(std::error_code, ::asio::const_buffer)>;

    explicit receiver(::asio::io_context& io_ctx, std::size_t buffer_size) : sock_{io_ctx}
    {
      buffer_.resize(buffer_size);
    }

    void open(socket sock)
    {
      auto protocol = sock.local_endpoint().protocol();
      sock_.assign(protocol, sock.release());
    }

    void start(data_callback cb)
    {
      data_cb_ = std::move(cb);
      do_read();
    }

    ::asio::io_context& get_io_context() noexcept { return sock_.get_executor().context(); }

    socket& get_socket() noexcept { return sock_; }

  private:
    void do_read()
    {
      sock_.async_read_some(
        asio::buffer(buffer_), make_custom_alloc_handler(handler_memory_, [this](std::error_code ec, std::size_t n) {
          data_cb_(ec, ::asio::const_buffer{buffer_.data(), n});

          if (!ec)
          {
            do_read();
          }
        }));
    }

    socket sock_;
    dynamic_handler_memory handler_memory_;
    std::vector<char> buffer_;
    data_callback data_cb_;
  };

} // namespace rasio
