#include "receiver.hpp"
#include "handler_allocator.hpp"

#include <vector>

namespace rasio
{
  struct receiver::impl
  {
    dynamic_handler_memory handler_memory;
    std::vector<char> buffer;
    data_callback data_cb;
    socket* sock = nullptr;

    void do_read(std::shared_ptr<impl>&& self)
    {
      if (sock == nullptr)
      {
        return;
      }

      sock->async_read_some(
        asio::buffer(buffer),
        make_custom_alloc_handler(handler_memory, [self = std::move(self)](std::error_code ec, std::size_t n) mutable {
          if (self->data_cb)
          {
            self->data_cb(ec, ::asio::const_buffer{self->buffer.data(), n});
          }

          if (!ec)
          {
            self->do_read(std::move(self));
          }
        }));
    }
  };

  receiver::receiver(::asio::io_context& io_ctx, std::size_t buffer_size)
    : sock_{io_ctx}, impl_{std::make_shared<impl>()}
  {
    impl_->sock = &sock_;
    impl_->buffer.resize(buffer_size);
  }

  receiver::~receiver()
  {
    impl_->data_cb = nullptr;
    impl_->sock = nullptr;
    sock_.close();
  }

  void receiver::open(socket sock)
  {
    auto protocol = sock.local_endpoint().protocol();
    sock_.assign(protocol, sock.release());
  }

  void receiver::start(data_callback cb)
  {
    impl_->data_cb = std::move(cb);
    impl_->do_read(std::shared_ptr<impl>{impl_});
  }
} // namespace rasio