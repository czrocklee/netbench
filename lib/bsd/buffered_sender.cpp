#include "buffered_sender.hpp"
#include <stdexcept>
#include <sys/uio.h>
#include <cstdio>
#include <iostream>

namespace bsd
{

  buffered_sender::buffered_sender(io_context& io_ctx, std::size_t max_buf_size)
    : io_ctx_{io_ctx}, write_list_{max_buf_size}
  {
  }

  void buffered_sender::open(utility::ref_or_own<socket> sock)
  {
    sock_ = std::move(sock);
  }

  void buffered_sender::send(void const* data, size_t size)
  {
    std::size_t bytes_sent = 0;
    bool is_empty = write_list_.empty();

    if (is_empty)
    {
      bytes_sent = get_socket().send(data, size, MSG_DONTWAIT | MSG_NOSIGNAL);

      if (bytes_sent == size)
        return;
    }

    auto bytes_remain = size - bytes_sent;

    if (write_list_.capacity() - write_list_.size() < bytes_remain)
    {
      throw std::runtime_error("buffered_sender: insufficient buffer capacity");
    }

    auto const* data_ptr = static_cast<std::byte const*>(data);
    write_list_.insert(write_list_.end(), data_ptr + bytes_sent, data_ptr + size);
    std::cout << "buffered_sender: added " << bytes_remain
              << " bytes to the buffer, total size now: " << write_list_.size() << std::endl;

    if (is_empty)
    {
      write_event_ = io_ctx_.register_event(get_socket().get_fd(), EPOLLOUT, &buffered_sender::on_events, this);
    }
  }

  void buffered_sender::on_events(std::uint32_t events, void* context)
  {
    auto& self = *static_cast<buffered_sender*>(context);

    if (events & EPOLLOUT)
    {
      self.handle_write();
    }
  }

  void buffered_sender::handle_write()
  {
    if (!write_list_.empty())
    {
      auto array_one = write_list_.array_one();
      auto array_two = write_list_.array_two();
      ::iovec iov[2]{{array_one.first, array_one.second}, {array_two.first, array_two.second}};

      try
      {
        if (auto bytes_sent = get_socket().send(iov, 2, MSG_DONTWAIT | MSG_NOSIGNAL); bytes_sent > 0)
        {
          if (bytes_sent == write_list_.size())
          {
            write_list_.clear();
            write_event_.reset();
          }
          else
          {
            write_list_.erase_begin(bytes_sent);
          }
        }
      }
      catch (std::system_error const& e)
      {
        fprintf(stderr, "Send error: %s\n", e.what());
      }
    }
  }
} // namespace bsd
