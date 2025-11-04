#include "connection.hpp"
#include <utility/time.hpp>
#include <asio/socket_base.hpp>
#include "../common/metadata.hpp"

#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
#include <sys/socket.h>

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

  std::fill_n(iov_, IOV_MAX + 1, ::iovec{.iov_base = msg_.data(), .iov_len = msg_.size()});
  std::fill_n(msg_hdrs_, IOV_MAX, ::mmsghdr{});

  msg_hdrs_[0].msg_hdr.msg_iov = iov_;

  for (int b = 1; b < IOV_MAX; ++b)
  {
    msg_hdrs_[b].msg_hdr.msg_iov = &iov_[1];
  }
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
  std::size_t msgs_to_send = std::min(count, static_cast<std::size_t>(IOV_MAX * IOV_MAX));

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
    // Fast path: within one bundle
    if (msgs_to_send <= static_cast<std::size_t>(IOV_MAX))
    {
      bytes_sent = sock_.send(iov_, msgs_to_send, 0);
    }
    else // Multi-bundle path using sendmmsg; reuse iov_ with iov_[1..] as the full_iov template
    {
      std::size_t const bundles = (msgs_to_send + IOV_MAX - 1) / IOV_MAX;

      for (std::size_t b = 0; b < bundles; ++b)
      {
        std::size_t this_bundle_msgs = std::min(msgs_to_send, static_cast<std::size_t>(IOV_MAX));
        msg_hdrs_[b].msg_hdr.msg_iovlen = this_bundle_msgs;
        msgs_to_send -= this_bundle_msgs;
      }

      auto const msgs_sent = sock_.sendmmsg(msg_hdrs_, bundles, 0);

      for (int i = 0; i < msgs_sent; ++i)
      {
        bytes_sent += msg_hdrs_[i].msg_len;
      }
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Connection " << conn_id_ << ": Send failed: " << e.what() << std::endl;
    std::terminate();
  }

  total_sent_bytes_ += bytes_sent;
  
  if (bytes_to_drain_ >= 0)//&& (bytes_to_drain_ += bytes_sent) > 1024 * 64)
  {
    try_drain_socket();
    //bytes_to_drain_ = 0;
  }

  if (bytes_sent < iov_[0].iov_len)
  {
    iov_[0].iov_base = reinterpret_cast<std::byte*>(iov_[0].iov_base) + bytes_sent;
    iov_[0].iov_len -= bytes_sent;
    return 0;
  }

  auto const bytes_sent_except_first = bytes_sent - iov_[0].iov_len;
  auto const offset = bytes_sent_except_first % msg_.size();
  iov_[0].iov_base = msg_.data() + offset;
  iov_[0].iov_len = msg_.size() - offset;
  return 1 + bytes_sent_except_first / msg_.size();
}

void connection::set_socket_recv_buffer_size(int size)
{
  sock_.set_option(::asio::socket_base::receive_buffer_size{size});
}

void connection::set_socket_send_buffer_size(int size)
{
  sock_.set_option(::asio::socket_base::send_buffer_size{size});
}

void connection::try_drain_socket()
{
  std::size_t bytes_read = 0;

  do
  {
    char dummy;
    bytes_read = sock_.recv(&dummy, 16 * 1024 * 1024, MSG_TRUNC);
    total_drained_bytes_ += bytes_read;
  } while (bytes_read > 0);
}