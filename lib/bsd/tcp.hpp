#pragma once

#include "socket.hpp"
#include "io_context.hpp"
#include "receiver.hpp"
#include "acceptor.hpp"

namespace bsd
{
  class tcp
  {
  public:
    using io_context = bsd::io_context;
    using socket = bsd::socket;
    using acceptor = bsd::acceptor;
    using receiver = bsd::receiver;
  };
}