#pragma once

#include <bsd/socket.hpp>
#include <bsd/io_context.hpp>
#include "../common/metadata.hpp"

#include <asio/buffer.hpp>
#include <utility/time.hpp>

#include <cstring>
#include <iostream>
#include <string>

class connection
{
public:
  connection(int conn_id, std::size_t msg_size);

  void connect(std::string const& host, std::string const& port, std::string const& bind_address = "");
  std::size_t try_send(std::size_t count);
  void close() { sock_.close(); }
  bool is_open() const { return sock_.get_fd() > 0; } 

  void enable_drain() { bytes_to_drain_ = 0; }
  void set_nodelay(bool enable) { sock_.set_nodelay(enable); }
  void set_socket_buffer_size(int size); 

  // Drain helpers for ensuring echoed data is fully received before close when drain is enabled
  std::size_t bytes_sent_total() const { return total_sent_bytes_; }
  std::size_t bytes_drained_total() const { return total_drained_bytes_; }
  void try_drain_socket();

private:

  int conn_id_;
  bsd::socket sock_;
  std::vector<std::byte> msg_;
  ::asio::const_buffer buf_;
  ::iovec iov_[IOV_MAX];
  int bytes_to_drain_ = -1;
  std::size_t total_sent_bytes_ = 0;
  std::size_t total_drained_bytes_ = 0;
};
