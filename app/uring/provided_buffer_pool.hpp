#pragma once

#include "io_context.hpp"

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <sys/mman.h>

namespace uring
{
  class provided_buffer_pool
  {
  public:
    provided_buffer_pool(io_context& io_ctx, size_t buffer_count, size_t buffer_size, uint16_t group_id);

    ~provided_buffer_pool();

    provided_buffer_pool(const provided_buffer_pool&) = delete;
    provided_buffer_pool& operator=(const provided_buffer_pool&) = delete;

    provided_buffer_pool(provided_buffer_pool&& other) noexcept;

    provided_buffer_pool& operator=(provided_buffer_pool&& other) noexcept;

    void reprovide_buffer(uint16_t buffer_id);

    uint8_t* get_buffer_address(uint16_t buffer_id) const;

    uint16_t get_group_id() const { return group_id_; }

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
}