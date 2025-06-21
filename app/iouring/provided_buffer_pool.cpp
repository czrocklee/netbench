#include "provided_buffer_pool.hpp"
#include <utility> // For std::swap
#include <string.h>

provided_buffer_pool::provided_buffer_pool(
  struct io_uring* ring,
  size_t buffer_count,
  size_t buffer_size,
  uint16_t group_id)
  : ring_(ring),
    buf_ring_(nullptr),
    pool_memory_(nullptr),
    buffer_count_(buffer_count),
    buffer_size_(buffer_size),
    group_id_(group_id)
{

  // 1. Allocate a large, contiguous memory pool for all buffers.
  // Using mmap is a good choice as it provides page-aligned memory.
  size_t pool_size = buffer_count_ * buffer_size_;
  pool_memory_ =
    static_cast<uint8_t*>(mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
  if (pool_memory_ == MAP_FAILED)
  {
    throw std::runtime_error("Failed to allocate memory for buffer pool");
  }

  // 2. Register the memory pool with io_uring as a buffer ring.
  // liburing provides a helper function to simplify this.
  int ret;
  buf_ring_ = io_uring_setup_buf_ring(ring_, buffer_count_, group_id_, 0, &ret);
  if (!buf_ring_)
  {
    munmap(pool_memory_, pool_size); // Clean up allocated memory
    throw std::runtime_error("Failed to setup io_uring buffer ring. Error: " + std::string(strerror(-ret)));
  }

  // 3. Populate the buffer ring with individual buffers from our pool.
  for (size_t i = 0; i < buffer_count_; ++i)
  {
    uint8_t* buffer_start = pool_memory_ + (i * buffer_size_);
    // Add the buffer to the ring. The kernel will now be able to select it.
    // We use 'i' as the unique buffer ID.
    io_uring_buf_ring_add(
      buf_ring_, buffer_start, buffer_size_, static_cast<uint16_t>(i), io_uring_buf_ring_mask(buffer_count_), i);
  }

  // 4. Make the added buffers visible to the kernel by advancing the tail.
  io_uring_buf_ring_advance(buf_ring_, buffer_count_);
}

void provided_buffer_pool::cleanup()
{
  if (buf_ring_ && ring_)
  {
    io_uring_unregister_buf_ring(ring_, group_id_);
  }
  if (pool_memory_)
  {
    munmap(pool_memory_, buffer_count_ * buffer_size_);
  }
  buf_ring_ = nullptr;
  pool_memory_ = nullptr;
  ring_ = nullptr;
}

provided_buffer_pool::~provided_buffer_pool() { cleanup(); }

provided_buffer_pool::provided_buffer_pool(provided_buffer_pool&& other) noexcept
  : ring_(other.ring_),
    buf_ring_(other.buf_ring_),
    pool_memory_(other.pool_memory_),
    buffer_count_(other.buffer_count_),
    buffer_size_(other.buffer_size_),
    group_id_(other.group_id_)
{
  // Null out the other object's members to prevent its destructor
  // from double-freeing the resources.
  other.ring_ = nullptr;
  other.buf_ring_ = nullptr;
  other.pool_memory_ = nullptr;
}

provided_buffer_pool& provided_buffer_pool::operator=(provided_buffer_pool&& other) noexcept
{
  if (this != &other)
  {
    // Clean up our own resources first
    cleanup();

    // Steal resources from the other object
    ring_ = other.ring_;
    buf_ring_ = other.buf_ring_;
    pool_memory_ = other.pool_memory_;
    buffer_count_ = other.buffer_count_;
    buffer_size_ = other.buffer_size_;
    group_id_ = other.group_id_;

    // Null out the other object
    other.ring_ = nullptr;
    other.buf_ring_ = nullptr;
    other.pool_memory_ = nullptr;
  }
  return *this;
}

void provided_buffer_pool::reprovide_buffer(uint16_t buffer_id)
{
  uint8_t* buffer_address = get_buffer_address(buffer_id);
  if (buffer_address)
  {
    // This is the efficient part: we just write to a shared memory ring
    // to return the buffer. No syscall needed.
    io_uring_buf_ring_add(buf_ring_, buffer_address, buffer_size_, buffer_id, io_uring_buf_ring_mask(buffer_count_), 0);
    // Advance the tail by 1 to make the single buffer visible.
    io_uring_buf_ring_advance(buf_ring_, 1);
  }
}

uint8_t* provided_buffer_pool::get_buffer_address(uint16_t buffer_id) const
{
  if (buffer_id >= buffer_count_)
  {
    return nullptr; // Invalid ID
  }
  return pool_memory_ + (buffer_id * buffer_size_);
}
