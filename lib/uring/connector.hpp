#pragma once

#include "bsd/socket.hpp"
#include "io_context.hpp"

#include <string>

namespace uring
{
  class connector
  {
  public:
    explicit connector(io_context& io_ctx);

    bsd::socket connect(std::string const& host, std::string const& port);

  private:
    io_context& io_ctx_;
  };
}