#pragma once

#include "socket.hpp"
#include "io_context.hpp"

#include <string>

namespace bsd
{
  class connector
  {
  public:
    explicit connector(io_context& io_ctx);

    socket connect(std::string const& host, std::string const& port);

  private:
    io_context& io_ctx_;
  };
}