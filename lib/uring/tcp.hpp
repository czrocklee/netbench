#pragma once

#include "socket.hpp"
#include "io_context.hpp"
#include "receiver.hpp"
#include "bundle_receiver.hpp"
#include "acceptor.hpp"
#include "connector.hpp"
#include "sender.hpp"

namespace uring
{
  class tcp
  {
  public:
    using io_context = uring::io_context;
    using socket = uring::socket;
    using acceptor = uring::acceptor;
    using connector = uring::connector;
    using receiver = uring::receiver;
    using sender = uring::sender;
  };
}