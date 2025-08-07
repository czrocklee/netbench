#pragma once

#include "bsd/socket.hpp"
#include "bsd/io_context.hpp"
#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <functional>
#include <system_error>
#include <vector>

namespace bsd
{
  class receiver
  {
  public:
    using data_callback = std::function<void(std::error_code, ::asio::const_buffer)>;

    explicit receiver(io_context& io_ctx, std::size_t buffer_size);
    void open(bsd::socket sock);
    void start(data_callback&& cb);
    void set_read_limit(std::size_t limit) noexcept { read_limit_ = limit; }
    [[nodiscard]] socket& get_socket() noexcept { return sock_; }
    [[nodiscard]] io_context& get_io_context() noexcept { return io_ctx_; }

  private:
    static void on_events(uint32_t events, void* context);
    void handle_events(uint32_t events);
    void do_read();

    io_context& io_ctx_;
    bsd::socket sock_;
    std::vector<char> buffer_;
    data_callback data_cb_;
    std::size_t read_limit_ = 0;
    io_context::event_handle read_evt_;
  };
}
