#pragma once

#include "../bsd/socket.hpp"

#include <cstring>
#include <iostream>
#include <string>

class connection
{
public:
  connection(int conn_id) : conn_id_{conn_id}, sock_{AF_INET, SOCK_STREAM, 0} {}

  void connect(const std::string& host, const std::string& port)
  {
    try
    {
      sock_.connect(host, port);
    }
    catch (const std::exception& e)
    {
      std::cerr << "Connection " << conn_id_ << ": Connect failed: " << e.what() << std::endl;
      std::terminate();
    }
  }

  void send(const char* data, std::size_t size)
  {
    ssize_t bytes_sent = 0;
    
    try
    {
      bytes_sent = sock_.send(data, size, 0);
    }
    catch (const std::exception& e)
    {
      std::cerr << "Connection " << conn_id_ << ": Send failed: " << e.what() << std::endl;
      std::terminate();
    }

    if (static_cast<std::size_t>(bytes_sent) != size)
    {
      std::cerr << "Connection " << conn_id_ << ": Partial send. Sent " << bytes_sent << " of " << size << " bytes."
                << std::endl;
    }
  }

private:
  int conn_id_;
  bsd::socket sock_;
};
