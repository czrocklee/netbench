#pragma once

#include <asio/buffer.hpp>
#include <cstdint>

struct metadata
{
  std::uint64_t msg_size;
};

template<typename Socket>
void send_metadata(Socket& sock, metadata const& md)
{
  if (sock.send(::asio::buffer(&md, sizeof(md)), 0) != sizeof(md))
  {
    throw std::runtime_error{"Failed to send metadata"};
  }
}

template<typename Socket>
void recv_metadata(Socket& sock, metadata& md)
{
  if (sock.receive(::asio::buffer(&md, sizeof(md)), 0) != sizeof(md))
  {
    throw std::runtime_error{"Failed to receive metadata"};
  }
}