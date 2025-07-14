#pragma once

#include "bsd/socket.hpp"

#include <cstring>
#include <iostream>
#include <string>

class connection
{
public:
  connection(int conn_id) : conn_id_{conn_id}, sock_{AF_INET, SOCK_STREAM, 0} {}

  void connect(const std::string& host, const std::string& port, const std::string& bind_address = "")
  {
    if (!bind_address.empty())
    {
      try
      {
        sock_.bind(bind_address, "0");
      }
      catch (const std::exception& e)
      {
        std::cerr << "Connection " << conn_id_ << ": Bind failed: " << e.what() << std::endl;
        std::terminate();
      }
    }

    try
    {
      sock_.connect(host, port);
      sock_.set_nonblocking(true);
    }
    catch (const std::exception& e)
    {
      std::cerr << "Connection " << conn_id_ << ": Connect failed: " << e.what() << std::endl;
      std::terminate();
    }
  }

  void set_nodelay(bool enable)
  {
    sock_.set_nodelay(enable);
  }

  bool send(const char* data, std::size_t size)
  {
    ssize_t bytes_sent = 0;
    auto to_sent = bytes_remains_ > 0 ? bytes_remains_ : size;

    try
    {
      bytes_sent = sock_.send(data + bytes_remains_, to_sent, 0);
    }
    catch (const std::exception& e)
    {
      std::cerr << "Connection " << conn_id_ << ": Send failed: " << e.what() << std::endl;
      std::terminate();
    }

    bytes_remains_ = to_sent - static_cast<std::size_t>(bytes_sent);
    return bytes_remains_ == 0;
  }

private:
  int conn_id_;
  bsd::socket sock_;
  std::size_t bytes_remains_ = 0;
};
