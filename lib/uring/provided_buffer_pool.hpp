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
    provided_buffer_pool(io_context& io_ctx, std::size_t buffer_count, std::size_t buffer_size, std::uint16_t group_id);

    ~provided_buffer_pool();

    provided_buffer_pool(const provided_buffer_pool&) = delete;
    provided_buffer_pool& operator=(const provided_buffer_pool&) = delete;
    provided_buffer_pool(provided_buffer_pool&& other) = delete;
    provided_buffer_pool& operator=(provided_buffer_pool&& other) = delete;

    void populate_buffers();


    void reprovide_buffer(std::uint16_t buffer_id);

    void reprovide_buffers(std::uint16_t buffer_id_begin, std::uint16_t buffer_id_end);

    std::uint8_t* get_buffer_address(std::uint16_t buffer_id) const;

    std::uint16_t get_group_id() const { return group_id_; }

    std::size_t get_buffer_size() const { return buffer_size_; }

    std::size_t get_buffer_count() const { return buffer_count_; }

  private:
    void cleanup();

    ::io_uring* ring_;
    ::io_uring_buf_ring* buf_ring_;
    std::uint8_t* pool_memory_;
    std::size_t buffer_count_;
    std::size_t buffer_size_;
    std::uint16_t group_id_;
  };
}