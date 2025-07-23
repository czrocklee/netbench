#pragma once

#include "bsd/socket.hpp"
#include "io_context.hpp"
#include "receiver.hpp"
#include "acceptor.hpp"
#include "connector.hpp"

namespace uring
{
  class tcp
  {
  public:
    using io_context = uring::io_context;
    using socket = bsd::socket;
    using acceptor = uring::acceptor;
    using connector = uring::connector;
    using receiver = uring::receiver;
  };
}