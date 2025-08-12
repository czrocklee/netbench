#include "bundle_receiver.hpp"
#include <utility>
#include <cstring>
#include <iostream>

namespace uring
{

  bundle_receiver::bundle_receiver(io_context& io_ctx, provided_buffer_pool& buffer_pool)
    : io_ctx_{io_ctx}, buffer_pool_{buffer_pool}
  {
    bundle_.reserve(buffer_pool.get_buffer_count());
  }

  bundle_receiver::~bundle_receiver()
  {

  }

  void bundle_receiver::open(bsd::socket sock)
  {
    sock_ = std::move(sock);
  }

  void bundle_receiver::start(data_callback cb)
  {
    data_cb_ = std::move(cb);
    new_bundle_recv_op();
  }

  void bundle_receiver::on_bundle_recv(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<bundle_receiver*>(context);
    self.bundle_.clear();

    if (cqe.res <= 0)
    {
      if (cqe.res == -ENOBUFS)
      {
        // Handle the case where no buffers are available
        // std::cerr << "No buffers available for receiving data. fd=" << self.sock_.get_fd() << std::endl;
        if (!(cqe.flags & IORING_CQE_F_MORE)) { self.new_bundle_recv_op(); }
        return;
      }

      std::error_code ec = (cqe.res < 0) ? std::make_error_code(static_cast<std::errc>(-cqe.res))
                                         : ::asio::error::make_error_code(::asio::error::eof);
      self.data_cb_(ec, self.bundle_);
      return;
    }

    using buffer_id_type = provided_buffer_pool::buffer_id_type;
    auto buf_id = buffer_id_type{static_cast<buffer_id_type::value_type>(cqe.flags >> IORING_CQE_BUFFER_SHIFT)};
    auto buf_id_begin = buf_id;
    std::size_t bytes_received = static_cast<std::size_t>(cqe.res);

    /*     std::cout << "Received bundle with " << bytes_received << " bytes, starting from buffer ID " << buf_id_start
                  << std::endl; */

    do {
      std::byte const* address = self.buffer_pool_.get_buffer_address(buf_id);
      std::size_t const size = std::min(bytes_received, self.buffer_pool_.get_buffer_size());
      self.bundle_.emplace_back(address, size);
      bytes_received -= size;

      if (++buf_id == self.buffer_pool_.get_buffer_count()) { buf_id = buffer_id_type{0}; }

    } while (bytes_received > 0);

    self.data_cb_({}, self.bundle_);

    // std::cout << "Recycling buffers: [" << buf_id_start.value() << ", " << buf_id.value() << ") for connection " <<
    // self.sock_.get_fd() << std::endl;
    self.buffer_pool_.push_buffers(buf_id_begin, buf_id);
    /*     auto buf_id_end = buf_id;
    if (buf_id_end > buf_id_begin)
    {
      for (auto i = buf_id_begin; i < buf_id_end; ++i) { self.buffer_pool_.push_buffer(i); }
    }
    else
    {
      for (auto i = buf_id_begin; i < self.buffer_pool_.get_buffer_count(); ++i) { self.buffer_pool_.push_buffer(i); }
      for (auto i = buffer_id_type{0}; i < buf_id_end; ++i) { self.buffer_pool_.push_buffer(i); }
    } */

    if (!(cqe.flags & IORING_CQE_F_MORE)) { self.new_bundle_recv_op(); }
  }

  void bundle_receiver::new_bundle_recv_op()
  {
    recv_handle_ = io_ctx_.create_request(on_bundle_recv, this);
    auto& sqe = recv_handle_.get_sqe();
    io_uring_prep_recv_multishot(&sqe, sock_.get_fd(), NULL, 0, 0);
    sqe.flags |= IOSQE_BUFFER_SELECT;
    sqe.flags |= IOSQE_FIXED_FILE;

    sqe.buf_group = buffer_pool_.get_group_id();
    sqe.ioprio |= IORING_RECVSEND_BUNDLE;
  }

} // namespace uring
