#pragma once

#include <deque>
#include <vector>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include "io_context.hpp"
#include "socket.hpp"
#include "utility/ref_or_own.hpp"
#include <boost/circular_buffer.hpp>

namespace bsd
{
  class buffered_sender
  {
  public:
    buffered_sender(io_context& io_ctx, std::size_t max_buf_size = 1024 * 1024 * 4);
    void open(utility::ref_or_own<socket> sock);
    void send(void const* data, size_t size);
    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }
    [[nodiscard]] io_context& get_io_context() noexcept { return io_ctx_; }

  private:
    static void on_events(std::uint32_t events, void* context);
    void handle_write();

    io_context& io_ctx_;
    utility::ref_or_own<socket> sock_;
    boost::circular_buffer<std::byte> write_list_;
    io_context::event_handle write_event_;
  };
} // namespace bsd
