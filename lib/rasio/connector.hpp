#pragma once

#include "socket.hpp"
#include <asio/io_context.hpp>

#include <string>

namespace rasio
{
  class connector
  {
  public:
    explicit connector(::asio::io_context& io_ctx);

    socket connect(std::string const& host, std::string const& port);

  private:
    ::asio::io_context& io_ctx_;
  };
}