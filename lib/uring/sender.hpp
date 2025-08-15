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

    sender(io_context& io_ctx, registered_buffer_pool& buf_pool, std::size_t max_writelist_entries = 1024 * 4);

    void open(socket_type sock);
    template<typename F>
    void send(std::size_t size, F&& f);
    void send(void const* data, std::size_t size);

    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }

  private:
    static void on_send_completion(::io_uring_cqe const& cqe, void* context);

    void start_send_operation(void const* data, std::size_t size, buffer_index_type buf_index);

    io_context& io_ctx_;
    socket_type sock_;
    registered_buffer_pool& buf_pool_;
    io_context::request_handle send_handle_;

    struct buffer_data
    {
      buffer_index_type index;
      std::size_t size;
    };

    buffer_data head_buf_;
    io_context::submit_sequence last_sub_seq_;
    ::io_uring_sqe* last_send_sqe_ = nullptr;
    boost::circular_buffer<buffer_data> write_list_;
    boost::circular_buffer<buffer_index_type> pending_zc_notif_;

    enum class state
    {
      idle,
      zc_sending,
      zc_confirmed,
      retrying
    };

    state state_ = state::idle;
  };

  template<typename F>
  void sender::send(std::size_t size, F&& f)
  {
    auto const acquire_new_buf = [&](buffer_data& buf_data) {
      buf_data.index = buf_pool_.acquire_buffer();
      auto buf = buf_pool_.get_buffer(buf_data.index);
      buf_data.size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
      //std::cout << "buffer acquired " << buf_data.index << std::endl;  
      return buf;
    };

    if (state_ == state::idle)
    {
      auto const buf = acquire_new_buf(head_buf_);
      start_send_operation(buf.data(), head_buf_.size, head_buf_.index);
      return;
    }

    auto const append_to_buf = [&](buffer_data& buf_data) {
      auto buf = buf_pool_.get_buffer(buf_data.index);
      buf += buf_data.size;
      buf_data.size += std::invoke(std::forward<F>(f), buf.data(), buf.size());
    };

    if (write_list_.empty())
    {
      if (last_sub_seq_ == io_ctx_.get_submit_sequence())
      {
        if (auto const head_space = buf_pool_.get_buffer_size() - head_buf_.size; head_space >= size)
        {
          append_to_buf(head_buf_);
          last_send_sqe_->len = head_buf_.size; // update the send length
          //std::cout << "update length to " << last_send_sqe_->len << std::endl;

          return;
        }
      }

      write_list_.push_back();
      acquire_new_buf(write_list_.back());

      std::cout << "write_list started to be built " << write_list_.size() << std::endl;
      return;
    }

    if (auto const tail_space = buf_pool_.get_buffer_size() - write_list_.back().size; tail_space >= size)
    {
      append_to_buf(write_list_.back());
      return;
    }

    if (write_list_.full()) { throw std::runtime_error("sender: write list is full"); }

    write_list_.push_back();
    acquire_new_buf(write_list_.back());
    std::cout << "write_list keep building " << write_list_.size() << std::endl;
  }
}
