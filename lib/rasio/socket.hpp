#pragma once

#include <asio/basic_stream_socket.hpp>
#include <asio/ip/tcp.hpp>

namespace rasio
{
  using socket = ::asio::basic_stream_socket<::asio::ip::tcp, ::asio::io_context::executor_type>;
}