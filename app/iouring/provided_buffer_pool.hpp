#pragma once

#include <liburing.h>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <sys/mman.h>

/**
 * @class provided_buffer_pool
 * @brief Manages a pool of provided buffers for io_uring using the modern
 * and efficient registered buffer ring mechanism.
 *
 * This class handles memory allocation, registration with the io_uring kernel
 * interface, and provides a simple way to return buffers to the pool after use.
 * It follows RAII principles for automatic resource management.
 */
class provided_buffer_pool
{
public:
  /**
   * @brief Constructs and initializes the buffer pool.
   *
   * Allocates a contiguous block of memory, registers it with io_uring as a
   * buffer ring, and populates the ring with individual buffers.
   *
   * @param ring A pointer to the initialized io_uring instance.
   * @param buffer_count The total number of buffers in the pool.
   * @param buffer_size The size of each individual buffer.
   * @param group_id The buffer group ID to associate with this pool.
   * @throws std::runtime_error if memory allocation or registration fails.
   */
  provided_buffer_pool(struct io_uring* ring, size_t buffer_count, size_t buffer_size, uint16_t group_id);

  /**
   * @brief Destructor. Unregisters the buffer ring and frees the memory.
   */
  ~provided_buffer_pool();

  // --- Rule of Five: Movable but not Copyable ---

  // Delete copy constructor and copy assignment operator to prevent
  // accidental duplication of managed resources.
  provided_buffer_pool(const provided_buffer_pool&) = delete;
  provided_buffer_pool& operator=(const provided_buffer_pool&) = delete;

  /**
   * @brief Move constructor.
   */
  provided_buffer_pool(provided_buffer_pool&& other) noexcept;

  /**
   * @brief Move assignment operator.
   */
  provided_buffer_pool& operator=(provided_buffer_pool&& other) noexcept;

  /**
   * @brief Returns a buffer to the pool, making it available for the kernel to use.
   *
   * This should be called after the application has finished processing the data
   * in the buffer received from a completion event.
   *
   * @param buffer_id The ID of the buffer to return, obtained from the CQE flags.
   */
  void reprovide_buffer(uint16_t buffer_id);

  /**
   * @brief Gets the memory address of a specific buffer.
   * @param buffer_id The ID of the buffer.
   * @return A pointer to the start of the buffer's memory.
   */
  uint8_t* get_buffer_address(uint16_t buffer_id) const;

  /**
   * @brief Gets the buffer group ID of this pool.
   */
  uint16_t get_group_id() const { return group_id_; }

  /**
   * @brief Gets the size of individual buffers in the pool.
   */
  size_t get_buffer_size() const { return buffer_size_; }

private:
  void cleanup();

  struct io_uring* ring_;
  struct io_uring_buf_ring* buf_ring_;
  uint8_t* pool_memory_;
  size_t buffer_count_;
  size_t buffer_size_;
  uint16_t group_id_;
};
