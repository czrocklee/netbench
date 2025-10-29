#pragma once

#include "socket.hpp"

#include <asio/io_context.hpp>
#include <functional>
#include <memory>
#include <system_error>
#include <string>

namespace rasio
{
  class acceptor final
  {
  public:
    using accept_callback = std::function<void(std::error_code, socket)>;

    explicit acceptor(::asio::io_context& io_ctx);
    ~acceptor();
    
    void listen(std::string const& address, std::string const& port);
    void start(accept_callback cb);

  private:
    struct impl;
    std::shared_ptr<impl> impl_;
  };
} // namespace rasio
