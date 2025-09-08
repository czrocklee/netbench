#pragma once

#include "bsd/socket.hpp"
#include "io_context.hpp"

namespace uring
{
  class socket : public bsd::socket
  {
  public:
    socket() = default;
    socket(int fd) : bsd::socket{fd}, file_handle_{get_fd()} {}
    socket(int domain, int type, int protocol) : bsd::socket(domain, type, protocol), file_handle_{get_fd()} {}

    void fix_file_handle(io_context& io_ctx) noexcept { file_handle_ = io_ctx.create_fixed_file(get_fd()); }
    io_context::file_handle const& get_file_handle() const noexcept { return file_handle_; }

  private:
    io_context::file_handle file_handle_;
  };
}