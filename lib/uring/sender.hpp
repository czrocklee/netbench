#pragma once

#include "socket.hpp"
#include "registered_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>
#include <utility/logger.hpp>

#include <boost/circular_buffer.hpp>
#include <functional>
#include <iostream>
#include <cassert>
#include <deque>

namespace uring
{
  class sender
  {
  public:
    using buffer_index_type = registered_buffer_pool::buffer_index_type;
    using socket_type = utility::ref_or_own<socket>;

    sender(io_context& io_ctx, registered_buffer_pool& buf_pool, std::size_t max_buf_size = 1024 * 1024 * 64);

    enum flags : int
    {
      zerocopy = 1 << 0
    };

    void open(socket_type sock, flags f = static_cast<flags>(0));
    template<typename F>
    void send(std::size_t size, F&& f);
    void send(void const* data, std::size_t size);

    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }

  private:
    void start_send_operation();
    static void on_send_completion(::io_uring_cqe const& cqe, void* context);
    static void on_zc_send_completion(::io_uring_cqe const& cqe, void* context);
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

    flags flags_;
    int send_error_ = 0;
    int pending_zf_notify_ = 0;
    bool sending_ = false;
    boost::circular_buffer<buffer_data> write_list_;
    std::size_t active_index_ = 0;
    io_context::submit_sequence last_sub_seq_;
    ::io_uring_sqe* last_send_sqe_ = nullptr;
  };

  template<typename F>
  void sender::send(std::size_t size, F&& f)
  {
    if (send_error_ > 0) { throw std::runtime_error("send failed: " + std::string(strerror(send_error_))); }

    auto const create_new_buf = [&] {
      write_list_.push_back();
      auto& data = write_list_.back();
      data.index = buf_pool_.acquire_buffer();
      auto buf = buf_pool_.get_buffer(data.index);
      data.size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
    };

    if (write_list_.empty())
    {
      LOG_TRACE("activating new buffer: size={}, is_sending={}", size, sending_);
      create_new_buf();
      active_index_ = 0;
      start_send_operation();
      return;
    }

    auto& data = write_list_.back();
    auto buf = buf_pool_.get_buffer(data.index);
    buf += (data.offset + data.size);
    LOG_TRACE(
      "buffer status: index={}, offset={}, size={}, remains={}, active_index={}",
      data.index.value(),
      data.offset,
      data.size,
      buf.size(),
      active_index_);

    if (buf.size() >= size)
    {
      data.size += std::invoke(std::forward<F>(f), buf.data(), buf.size());

      if (write_list_.size() == active_index_ + 1)
      {
        if (last_sub_seq_ == io_ctx_.get_submit_sequence())
        {
          LOG_TRACE("appending to front buffer and updating last send SQE: size={}, new size: {}", size, data.size);
          last_send_sqe_->len = data.size; // update the send length
          return;
        }

        if (!sending_)
        {
          LOG_TRACE(
            "appending to front buffer and starting new send operation: size={}, active_size={}", size, data.size);
          start_send_operation();
        }
        else
        {
          LOG_TRACE(
            "appending to front buffer while sending already started: size={}, active_size={}", size, data.size);
        }

        return;
      }

      LOG_TRACE(
        "appending to back buffer: size={}, active_size={}, back_size={}",
        size,
        write_list_[active_index_].size,
        data.size);
      return;
    }

    if (write_list_.full())
    {
      // std::terminate();
      throw std::runtime_error("sender: insufficient buffer space");
    }

    create_new_buf();

    LOG_TRACE(
      "appending to newly created back buffer: size={}, pending_size={}, is_sending={}", size, data.size, sending_);

    if (!sending_) { start_send_operation(); }
  }
}
