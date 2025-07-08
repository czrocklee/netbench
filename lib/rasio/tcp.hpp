#pragma once

#include "receiver.hpp"
#include "acceptor.hpp"
#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>

namespace rasio
{
  class tcp
  {
  public:
    using io_context = ::asio::io_context;
    using socket = ::asio::ip::tcp::socket;
    using acceptor = rasio::acceptor;
    using receiver = rasio::receiver;
  };
}