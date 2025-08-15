#pragma once

#include "socket.hpp"
#include "receiver.hpp"
#include "connector.hpp"
#include "acceptor.hpp"
#include "buffered_sender.hpp"

#include <asio/io_context.hpp>

namespace rasio
{
  class tcp
  {
  public:
    using io_context = ::asio::io_context;
    using socket = rasio::socket;
    using acceptor = rasio::acceptor;
    using connector = rasio::connector;
    using receiver = rasio::receiver;
    using sender = rasio::buffered_sender;
  };
}