#pragma once

#include "../bsd/socket.hpp"
#include "provided_buffer_pool.hpp"

#include <functional>

namespace uring
{
  class sender
  {
  public:
    sender(io_context& io_ctx, std::size_t buffer_count, std::size_t buffer_size, std::uint16_t group_id)
      : io_ctx_{io_ctx}, sock_{AF_INET, SOCK_STREAM, 0}, buffer_pool_{io_ctx, buffer_count, buffer_size, group_id}
    {
    }

    void open(bsd::socket&& sock) { sock_ = std::move(sock); }

    template<typename F>
    void send(F&& f)
    {
      void* const buffer_address = buffer_pool_.get_buffer_address(buffer_id_head_);
      const std::size_t buffer_size = buffer_pool_.get_buffer_size();
      std::size_t actual_size = std::invoke(std::forward<F>(f), buffer_address, buffer_size);

      auto& sqe = io_ctx_.create_request(send_req_data_);
      ::io_uring_prep_send(&sqe, sock_.get_fd(), buffer_address, actual_size, 0);
      sqe.buf_group = buffer_pool_.get_group_id();
      sqe.flags |= IOSQE_BUFFER_SELECT;

      buffer_id_head_ = (buffer_id_head_ + 1) % buffer_pool_.get_buffer_count();
      is_buffer_full_ = (buffer_id_head_ == buffer_id_tail_);
    }

    bool is_buffer_full() const { return is_buffer_full_; }

    provided_buffer_pool& get_buffer_pool() { return buffer_pool_; }

  private:
    void on_send(const ::io_uring_cqe& cqe, void* context)
    {
      auto* self = static_cast<sender*>(context);
      if (cqe.res < 0)
      {
        fprintf(stderr, "Send error on fd %d: %s\n", self->sock_.get_fd(), ::strerror(-cqe.res));
        return;
      }

      auto buffer_id = cqe.flags >> IORING_CQE_BUFFER_SHIFT;
      if (buffer_id != self->buffer_id_tail_)
      {
        fprintf(stderr, "invalid buffer id %d, expected %d\n", buffer_id, self->buffer_id_tail_);
      }

      if (++self->buffer_id_tail_ == self->buffer_pool_.get_buffer_count())
      {
        self->buffer_id_tail_ = 0;
      }
    }

    io_context& io_ctx_;
    bsd::socket sock_;
    provided_buffer_pool buffer_pool_;
    std::int16_t buffer_id_head_ = 0;
    std::int16_t buffer_id_tail_ = 0;
    bool is_buffer_full_ = false;
    io_context::req_data send_req_data_;
  };

}
