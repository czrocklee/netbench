#pragma once

#include "bsd/socket.hpp"
#include "../common/metadata.hpp"

#include <asio/buffer.hpp>
#include <cstring>
#include <iostream>
#include <string>
#include <utility/time.hpp>

class connection
{
public:
  connection(int conn_id, std::size_t msg_size) : conn_id_{conn_id}, sock_{AF_INET, SOCK_STREAM, 0}
  {
    if (msg_size < sizeof(std::uint64_t))
    {
      throw std::runtime_error{"Message size must be at least 8 bytes to store timestamp"};
    }

    msg_.resize(msg_size, std::byte{static_cast<unsigned char>('a' + conn_id_ % 26)});
  }

  void connect(std::string const& host, std::string const& port, std::string const& bind_address = "")
  {
    if (!bind_address.empty())
    {
      try
      {
        sock_.bind(bind_address, "0");
      }
      catch (std::exception const& e)
      {
        std::cerr << "Connection " << conn_id_ << ": Bind failed: " << e.what() << std::endl;
        std::terminate();
      }
    }

    try
    {
      sock_.connect(host, port);
      metadata const md{.msg_size = msg_.size()};
      sock_.send(::asio::buffer(&md, sizeof(md)), 0);
      sock_.set_nonblocking(true);
    }
    catch (std::exception const& e)
    {
      std::cerr << "Connection " << conn_id_ << ": Connect failed: " << e.what() << std::endl;
      std::terminate();
    }
  }

  void set_nodelay(bool enable) { sock_.set_nodelay(enable); }

  bool try_send()
  {
    if (buf_.size() == 0)
    {
      std::uint64_t const now = utility::nanos_since_epoch();
      std::memcpy(msg_.data(), &now, sizeof(now));
      buf_ = ::asio::buffer(msg_.data(), msg_.size());
    }

    ssize_t bytes_sent = 0;

    try
    {
      bytes_sent = sock_.send(buf_, 0);
    }
    catch (std::exception const& e)
    {
      std::cerr << "Connection " << conn_id_ << ": Send failed: " << e.what() << std::endl;
      std::terminate();
    }

    buf_ += bytes_sent;
    return buf_.size() == 0;
  }

private:
  int conn_id_;
  bsd::socket sock_;
  std::vector<std::byte> msg_;
  ::asio::const_buffer buf_;
};
