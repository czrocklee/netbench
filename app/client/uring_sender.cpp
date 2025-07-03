#include "uring_sender.hpp"

#include <iostream>
#include <numeric>
#include <random>

namespace client
{

  namespace 
  {
    uring_sender* self = nullptr;
  }

  uring_sender::uring_sender(
    int id,
    const std::string& host,
    const std::string& port,
    std::size_t msg_size,
    int msgs_per_sec)
    : id_{id},
      io_ctx_{65536 / 2},
      sock_{AF_INET, SOCK_STREAM, 0},
      buffer_pool_{io_ctx_, 16, msg_size, static_cast<std::uint16_t>(id)},
      send_req_data_{on_send_complete, this},
      interval_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec}

  {
    self = this;

    for (auto i = 0; i < buffer_pool_.get_buffer_count(); ++i)
    {
      auto* buffer = buffer_pool_.get_buffer_address(i);
      std::memset(buffer, 'a' + i % 26, msg_size);
    }

    sock_.connect(host, port);
  }

  uring_sender::~uring_sender() {}

  void uring_sender::start()
  {
    static std::random_device rd;
    start_time_ = std::chrono::steady_clock::now() +
                  std::chrono::nanoseconds{std::uniform_int_distribution<std::int64_t>{0, interval_.count()}(rd)};

    thread_ = std::jthread{[this] { run(); }};
  }

  uint64_t uring_sender::total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }

  uint64_t uring_sender::total_bytes_sent() const { return total_bytes_sent_.load(std::memory_order_relaxed); }

  void uring_sender::run()
  {

    int conn_idx = 0;

    while (true)
    {
      const auto now = std::chrono::steady_clock::now();
      const auto expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);
      // std::cout << interval_.count() << " " << (now - start_time_).count() << " " << expected_msgs << std::endl;

      if (msgs_requested_ < expected_msgs)
      {
        if (!is_buffer_full_)
        {
          void* const buffer_address = buffer_pool_.get_buffer_address(buffer_id_head_ % buffer_pool_.get_buffer_count());
          const std::size_t buffer_size = buffer_pool_.get_buffer_size();

          auto& sqe = io_ctx_.create_request(send_req_data_);
          ::io_uring_prep_send(&sqe, sock_.get_fd(), buffer_address, buffer_size, 0);
          sqe.buf_group = buffer_pool_.get_group_id();
          sqe.flags |= IOSQE_BUFFER_SELECT;
          buffer_pool_.reprovide_buffer(buffer_id_head_);
          
          auto* data = new uring::io_context::req_data{.handler = on_send_complete, .context = (void *)buffer_id_head_};
          ::io_uring_sqe_set_data(&sqe, data);


          //::io_uring_submit(io_ctx_.get_ring());
          std::cout << "submit " << buffer_id_head_ << " " << &sqe<< std::endl;

         // buffer_id_head_ = (buffer_id_head_ + 1) % buffer_pool_.get_buffer_count();
         // is_buffer_full_ = (buffer_id_head_ == buffer_id_tail_);
        
         ++buffer_id_head_;
         is_buffer_full_ = (buffer_id_head_ - buffer_id_tail_ == buffer_pool_.get_buffer_count());
         ++msgs_requested_;
        }
      }

      io_ctx_.poll();
    }
  }

  void uring_sender::on_send_complete(const ::io_uring_cqe& cqe, void* context)
  {
    //auto* self = static_cast<uring_sender*>(context);

    std::cout << "completion " << cqe.res << " " << reinterpret_cast<std::intptr_t>(context) << std::endl;


    if (cqe.res < 0)
    {
      fprintf(stderr, "Send error on fd %d: %s\n", self->sock_.get_fd(), ::strerror(-cqe.res));
      return;
    }

    auto buffer_id = cqe.flags >> IORING_CQE_BUFFER_SHIFT;

    if (buffer_id != self->buffer_id_tail_)
    {
      fprintf(stderr, "invalid buffer id %d, expected %d\n", buffer_id, self->buffer_id_tail_);
      std::terminate();
    }
    //std::cout << "completion " << cqe.res << " " << buffer_id << " " << reinterpret_cast<std::intptr_t>(context) << std::endl;

    self->total_msgs_sent_.store(self->total_msgs_sent_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    self->total_bytes_sent_.store(
      self->total_bytes_sent_.load(std::memory_order_relaxed) + self->buffer_pool_.get_buffer_size(),
      std::memory_order_relaxed);

    if (cqe.res == self->buffer_pool_.get_buffer_size())
    {
      ++self->buffer_id_tail_;


   /*    if (++self->buffer_id_tail_ == self->buffer_pool_.get_buffer_count())
      {
        self->buffer_id_tail_ = 0;
      } */
    }
  }
}