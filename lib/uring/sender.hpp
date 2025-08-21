#pragma once

#include "socket.hpp"
#include "registered_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>

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

    sender(io_context& io_ctx, registered_buffer_pool& buf_pool);

    enum flags : int { zerocopy = 1 << 0 };
    void open(socket_type sock, flags f = static_cast<flags>(0));
    template<typename F>
    void send(std::size_t size, F&& f);
    void send(void const* data, std::size_t size);

    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }

  private:
    void start_send_operation();
    static void on_send_completion(::io_uring_cqe const& cqe, void* context);
    static void on_zc_send_completion(::io_uring_cqe const& cqe, void* context);
    void on_zf_notify();

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
    bool sending_ = false;
    std::optional<buffer_data> active_buf_;
    std::optional<buffer_data> pending_buf_;
    io_context::submit_sequence last_sub_seq_;
    ::io_uring_sqe* last_send_sqe_ = nullptr;
  };

  template<typename F>
  void sender::send(std::size_t size, F&& f)
  {
    //std::cout << "send requested: " << size << " bytes, is_sending: " << sending_ << std::endl;

    if (send_error_ > 0)
    {
      throw std::runtime_error("send failed: " + std::string(strerror(send_error_)));
    }

    if (!active_buf_)
    {
      active_buf_.emplace().index = buf_pool_.acquire_buffer();
      auto buf = buf_pool_.get_buffer(active_buf_->index);
      active_buf_->size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
      start_send_operation();
      return;
    }

    if (pending_buf_)
    {
      auto buf = buf_pool_.get_buffer(pending_buf_->index);
      buf += (pending_buf_->offset + pending_buf_->size);
      //std::cout << "active buffer size: " << active_buf_->size << " pending buffer size: " << pending_buf_->size << std::endl;
      if (buf.size() < size)
      {
        std::terminate();
        throw std::runtime_error("sender: insufficient buffer space");
      }

      pending_buf_->size += std::invoke(std::forward<F>(f), buf.data(), buf.size());
      return;
    }

    auto buf = buf_pool_.get_buffer(active_buf_->index);
    buf += (active_buf_->offset + active_buf_->size);

    if (buf.size() >= size)
    {
      active_buf_->size += std::invoke(std::forward<F>(f), buf.data(), buf.size());

      if (last_sub_seq_ == io_ctx_.get_submit_sequence())
      {
        //std::cout << "Updating last send SQE with new size: " << active_buf_->size << std::endl;
        last_send_sqe_->len = active_buf_->size; // update the send length
        return;
      }

      if (!sending_)
      {
        //std::cout << "Starting new send operation with updated size: " << active_buf_->size << std::endl;
        start_send_operation();
      }

      return;
    }

    //std::terminate();


    pending_buf_.emplace().index = buf_pool_.acquire_buffer();
    buf = buf_pool_.get_buffer(pending_buf_->index);
    pending_buf_->size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
  }
}
