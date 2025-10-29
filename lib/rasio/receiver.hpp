#pragma once

#include "socket.hpp"
#include <asio/error.hpp>
#include <asio/buffer.hpp>
#include <functional>
#include <system_error>
#include <memory>

namespace rasio
{
  class receiver
  {
  public:
    using data_callback = std::function<void(std::error_code, ::asio::const_buffer)>;

    explicit receiver(::asio::io_context& io_ctx, std::size_t buffer_size);
    ~receiver();

    void open(socket sock);
    void start(data_callback cb);

    ::asio::io_context& get_io_context() noexcept { return sock_.get_executor().context(); };
    socket& get_socket() noexcept { return sock_; }

  private:
    socket sock_;
    struct impl;
    std::shared_ptr<impl> impl_;
  };

} // namespace rasio
