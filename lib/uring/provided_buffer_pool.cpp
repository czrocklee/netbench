#include "provided_buffer_pool.hpp"
#include <utility> // For std::swap
#include <string.h>

namespace uring
{
  provided_buffer_pool::provided_buffer_pool(
    io_context& io_ctx,
    std::size_t buffer_count,
    std::size_t buffer_size,
    std::uint16_t group_id)
    : ring_{io_ctx.get_ring()},
      buf_ring_{nullptr},
      pool_memory_{nullptr},
      buffer_count_{buffer_count},
      buffer_size_{buffer_size},
      group_id_{group_id}
  {
    std::size_t pool_size = buffer_count_ * buffer_size_;
    pool_memory_ =
      static_cast<std::uint8_t*>(::mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));

    if (pool_memory_ == MAP_FAILED)
    {
      throw std::runtime_error("Failed to allocate memory for buffer pool");
    }

    int ret;
    buf_ring_ = ::io_uring_setup_buf_ring(ring_, buffer_count_, group_id_, 0, &ret);

    if (!buf_ring_)
    {
      ::munmap(pool_memory_, pool_size); // Clean up allocated memory
      throw std::runtime_error("Failed to setup ::io_uring buffer ring. Error: " + std::string(strerror(-ret)));
    }
  }

  void provided_buffer_pool::cleanup()
  {
    if (buf_ring_ && ring_)
    {
      ::io_uring_unregister_buf_ring(ring_, group_id_);
    }

    if (pool_memory_)
    {
      ::munmap(pool_memory_, buffer_count_ * buffer_size_);
    }

    buf_ring_ = nullptr;
    pool_memory_ = nullptr;
    ring_ = nullptr;
  }

  provided_buffer_pool::~provided_buffer_pool() { cleanup(); }

  void provided_buffer_pool::populate_buffers()
  {
    for (std::size_t i = 0; i < buffer_count_; ++i)
    {
      std::uint8_t* buffer_start = pool_memory_ + (i * buffer_size_);
      ::io_uring_buf_ring_add(
        buf_ring_,
        buffer_start,
        buffer_size_,
        static_cast<std::uint16_t>(i),
        ::io_uring_buf_ring_mask(buffer_count_),
        i);
    }

    ::io_uring_buf_ring_advance(buf_ring_, buffer_count_);
  }

  void provided_buffer_pool::reprovide_buffer(std::uint16_t buffer_id)
  {
    if (std::uint8_t* buffer_address = get_buffer_address(buffer_id % buffer_count_); buffer_address != nullptr)
    {
      // This is the efficient part: we just write to a shared memory ring
      // to return the buffer. No syscall needed.
      ::io_uring_buf_ring_add(
        buf_ring_, buffer_address, buffer_size_, buffer_id, ::io_uring_buf_ring_mask(buffer_count_), 0);
      // Advance the tail by 1 to make the single buffer visible.
      ::io_uring_buf_ring_advance(buf_ring_, 1);
    }
  }

  void provided_buffer_pool::reprovide_buffers(std::uint16_t buffer_id_begin, std::uint16_t buffer_id_end)
  {
    if (buffer_id_end > buffer_id_begin)
    {
      for (std::uint16_t i = buffer_id_begin; i < buffer_id_end; ++i)
      {
        ::io_uring_buf_ring_add(
          buf_ring_, get_buffer_address(i), buffer_size_, i, ::io_uring_buf_ring_mask(buffer_count_), 0);
      }

      ::io_uring_buf_ring_advance(buf_ring_, buffer_id_end - buffer_id_begin);
      return;
    }

    for (std::uint16_t i = buffer_id_begin; i < buffer_count_; ++i)
    {
      ::io_uring_buf_ring_add(
        buf_ring_, get_buffer_address(i), buffer_size_, i, ::io_uring_buf_ring_mask(buffer_count_), 0);
    }

    for (std::uint16_t i = 0; i < buffer_id_end; ++i)
    {
      ::io_uring_buf_ring_add(
        buf_ring_, get_buffer_address(i), buffer_size_, i, ::io_uring_buf_ring_mask(buffer_count_), 0);
    }

    ::io_uring_buf_ring_advance(buf_ring_, buffer_count_ - buffer_id_begin + buffer_id_end);
  }

  std::uint8_t* provided_buffer_pool::get_buffer_address(std::uint16_t buffer_id) const
  {
    if (buffer_id >= buffer_count_)
    {
      return nullptr; // Invalid ID
    }

    return pool_memory_ + (buffer_id * buffer_size_);
  }
}