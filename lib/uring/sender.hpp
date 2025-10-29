#pragma once

#include "socket.hpp"
#include "registered_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>
#include <utility/logger.hpp>

#include <boost/circular_buffer.hpp>
#include <magic_enum/magic_enum.hpp>

#include <functional>
#include <iostream>
#include <cassert>

namespace uring
{
  class sender
  {
  public:
    using buffer_index_type = registered_buffer_pool::buffer_index_type;
    using socket_type = utility::ref_or_own<socket>;

    sender(io_context& io_ctx, std::size_t max_buf_size = 1024 * 1024 * 64);

    enum flags : int
    {
      none = 0,
      zerocopy = 1 << 0
    };

    void open(socket_type sock, flags f = static_cast<flags>(0));
    template<typename F>
    void send(std::size_t size, F&& f);
    void send(void const* data, std::size_t size);

    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }

  private:
    template<typename F>
    void append_write_list(std::size_t size, F&& f);
    void prepare_send_operation();
    void on_submit_send_operation(::io_uring_sqe& sqe);
    void on_send_completion(::io_uring_cqe const& cqe);
    void on_zc_send_completion(::io_uring_cqe const& cqe);
    void on_zf_notify(::io_uring_cqe const& cqe);

    io_context& io_ctx_;
    socket_type sock_;
    registered_buffer_pool& buf_pool_;
    io_context::request_handle send_handle_;

    struct buffer_data
    {
      buffer_index_type index;
      std::size_t offset = 0;
      std::size_t size = 0;
      std::size_t pending_zf_notify = 0;
    };

    enum class state
    {
      idle,
      open,
      submitted
    };

    state state_ = state::idle;
    flags flags_ = flags::none;
    int send_error_ = 0;
    boost::circular_buffer<buffer_data> write_list_;
    std::size_t active_index_ = 0;
    ::io_uring_sqe* last_send_sqe_ = nullptr;
  };

  template<typename F>
  void sender::send(std::size_t size, F&& f)
  {
    if (send_error_ > 0)
    {
      throw std::runtime_error{"send failed: " + std::string(strerror(send_error_))};
    }

    append_write_list(size, std::forward<F>(f));

    if (state_ == state::idle)
    {
      prepare_send_operation();
      return;
    }
  }

  template<typename F>
  void sender::append_write_list(std::size_t size, F&& f)
  {
    auto const create_new_buf = [&] {
      if (size > buf_pool_.get_buffer_size())
      {
        throw std::runtime_error{"sender: send size exceeds buffer size"};
      }

      write_list_.push_back();
      auto& data = write_list_.back();
      data.index = buf_pool_.acquire_buffer();
      auto buf = buf_pool_.get_buffer(data.index);
      data.size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
      LOG_TRACE(
        "acquire new buffer: state={}, index={}, offset={}, size={}, remains={}, active_index={}",
        magic_enum::enum_name(state_),
        data.index.value(),
        data.offset,
        data.size,
        buf.size(),
        active_index_);
    };

    if (write_list_.empty())
    {
      create_new_buf();
      return;
    }

    auto& data = write_list_.back();
    auto buf = buf_pool_.get_buffer(data.index);
    buf += (data.offset + data.size);

    if (buf.size() >= size)
    {
      data.size += std::invoke(std::forward<F>(f), buf.data(), buf.size());

      LOG_TRACE(
        "append to buffer: state={}, index={}, offset={}, size={}, remains={}, active_index={}",
        magic_enum::enum_name(state_),
        data.index.value(),
        data.offset,
        data.size,
        buf.size(),
        active_index_);
      return;
    }

    if (write_list_.full())
    {
      // std::terminate();
      throw std::runtime_error{"sender: insufficient buffer space"};
    }

    create_new_buf();
  }
}
