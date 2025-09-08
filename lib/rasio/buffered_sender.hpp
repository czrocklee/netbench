#pragma once

#include "socket.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/async_result.hpp>
#include <asio/write.hpp>
#include <asio/buffer.hpp>

#include <utility/ref_or_own.hpp>
#include <boost/circular_buffer.hpp>

#include <cstdint>
#include <cstring>
#include <system_error>
#include <iostream>

namespace rasio
{
  class buffered_sender
  {
  public:
    buffered_sender(::asio::io_context& io_ctx, std::size_t max_buf_size = 1024 * 1024 * 4)
      : io_ctx_{io_ctx}, write_list_{max_buf_size}
    {
    }

    void open(utility::ref_or_own<socket> sock) { sock_ = std::move(sock); }

    void send(void const* data, size_t size)
    {
      std::size_t bytes_sent = 0;
      bool is_empty = write_list_.empty();

      if (is_empty)
      {
        bytes_sent = sock_.get().write_some(asio::buffer(data, size));

        if (bytes_sent == size)
        {
          return;
        }
      }

      auto bytes_remain = size - bytes_sent;

      if (write_list_.capacity() - write_list_.size() < bytes_remain)
      {
        throw std::runtime_error("buffered_sender: insufficient buffer capacity");
      }

      auto const* data_ptr = static_cast<std::byte const*>(data);
      write_list_.insert(write_list_.end(), data_ptr + bytes_sent, data_ptr + size);

      if (is_empty)
      {
        handle_write();
      }
    }

    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }
    [[nodiscard]] ::asio::io_context& get_io_context() noexcept { return io_ctx_; }

  private:
    void handle_write()
    {
      if (write_list_.empty())
        return;

      auto const array_one = write_list_.array_one();
      auto const array_two = write_list_.array_two();
      auto const buffers = std::array<::asio::const_buffer, 2>{
        asio::buffer(array_one.first, array_one.second), asio::buffer(array_two.first, array_two.second)};

      ::asio::async_write(sock_.get(), buffers, [this](std::error_code ec, std::size_t bytes_transferred) {
        if (!ec)
        {
          write_list_.erase_begin(bytes_transferred);
          if (!write_list_.empty())
            handle_write();
        }
        else
        {
          std::cerr << "Error during write: " << ec.message() << std::endl;
        }
      });
    }

    ::asio::io_context& io_ctx_;
    utility::ref_or_own<socket> sock_;
    boost::circular_buffer<std::byte> write_list_;
  };
} // namespace rasio
