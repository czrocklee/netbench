#include "connection.hpp"
#include <iostream>
#include <cstring>
#include <utility/time.hpp>
#include "../common/metadata.hpp"

namespace
{
  static constexpr int timestamp_header_size = sizeof(std::uint64_t);
}

connection::connection(int conn_id, std::size_t msg_size) : conn_id_{conn_id}, sock_{AF_INET, SOCK_STREAM, 0}
{
  if (msg_size < timestamp_header_size)
  {
    throw std::runtime_error{"Message size must be at least 8 bytes to store timestamp"};
  }

  msg_.reserve(msg_size);
  msg_.resize(timestamp_header_size, std::byte{0});

  if (msg_size > timestamp_header_size)
  {
    msg_.emplace_back(std::byte{'-'});

    for (std::size_t i = 0; i < msg_size - timestamp_header_size - 1; ++i)
    {
      msg_.emplace_back(std::byte{static_cast<unsigned char>('a' + ((i + conn_id_) % 26))});
    }
  }

  std::fill_n(iov_, IOV_MAX, ::iovec{.iov_base = msg_.data(), .iov_len = msg_.size()});
}

void connection::connect(std::string const& host, std::string const& port, std::string const& bind_address)
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

std::size_t connection::try_send(std::size_t count)
{
  std::size_t msgs_to_send = std::min(count, static_cast<std::size_t>(IOV_MAX));

  // Here we just using a single message and using iovec to replicate it for a whole batch.

  if (auto sent = msg_.size() - iov_[0].iov_len; sent == 0 || sent >= timestamp_header_size)
  {
    std::uint64_t const now = utility::nanos_since_epoch();
    std::memcpy(msg_.data(), &now, timestamp_header_size);
  }
  else
  {
    // Just send the rest of the message, sending a whole batch with updated timestamp will corrupt the last one
    msgs_to_send = 1;
  }

  std::size_t bytes_sent = 0;

  try
  {
    bytes_sent = sock_.send(iov_, msgs_to_send, 0);
  }
  catch (std::exception const& e)
  {
    std::cerr << "Connection " << conn_id_ << ": Send failed: " << e.what() << std::endl;
    std::terminate();
  }

  if (bytes_to_drain_ >= 0 && (bytes_to_drain_ += bytes_sent) > 1024 * 16)
  {
    try_drain_socket();
    bytes_to_drain_ = 0;
  }

  if (bytes_sent < iov_[0].iov_len)
  {
    iov_[0].iov_base = reinterpret_cast<std::byte*>(iov_[0].iov_base) + bytes_sent;
    iov_[0].iov_len -= bytes_sent;
    return 0;
  }

  auto bytes_sent_except_first = bytes_sent - iov_[0].iov_len;
  auto offset = bytes_sent_except_first % msg_.size();
  iov_[0].iov_base = msg_.data() + offset;
  iov_[0].iov_len = msg_.size() - offset;
  return 1 + bytes_sent_except_first / msg_.size();
}

void connection::enable_drain()
{
  bytes_to_drain_ = 0;
}

void connection::try_drain_socket()
{
  std::size_t bytes_read = 0;

  do {
    char dummy;
    bytes_read = sock_.recv(&dummy, 1024 * 1024, MSG_TRUNC | MSG_DONTWAIT);
  } while (bytes_read > 0);
}
