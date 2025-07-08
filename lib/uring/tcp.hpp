#pragma once

#include "bsd/socket.hpp"
#include "io_context.hpp"
#include "receiver.hpp"
#include "acceptor.hpp"

namespace uring
{
  class tcp
  {
  public:
    using io_context = uring::io_context;
    using socket = bsd::socket;
    using acceptor = uring::acceptor;
    using receiver = uring::receiver;
  };
}