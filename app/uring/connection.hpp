#include "../bsd/socket.hpp"
#include "io_context.hpp"
#include "provided_buffer_pool.hpp"

namespace uring
{
  class connection
  {
  public:
    explicit connection(io_context& io_ctx, provided_buffer_pool& buffer_pool) :
      io_ctx_{io_ctx},
      buffer_pool_{buffer_pool},
      recv_req_data_{on_multishot_recv, this},
      is_closed_{false}
    {
    }

    void open(bsd::socket&& sock) { sock_ = std::move(sock); }

    void start() { new_multishot_recv_op(); }

    bool is_closed() const { return is_closed_; }

  private:
    static void on_multishot_recv(const ::io_uring_cqe& cqe, void* context)
    {
      auto* conn = reinterpret_cast<connection*>(context);

      if (cqe.res <= 0)
      {
        // Connection closed or error
        if (cqe.res < 0)
        {
          fprintf(stderr, "Recv error on fd %d: %s\n", conn->sock_.get_fd(), ::strerror(-cqe.res));
        }
        else
        {
          // This means the client gracefully closed the connection.
          printf("Client disconnected, fd: %d\n", conn->sock_.get_fd());
        }
        conn->is_closed_ = true;
        return; // Stop processing for this connection
      }

      // Successful receive
      if (!(cqe.flags & IORING_CQE_F_BUFFER))
      {
        fprintf(stderr, "Error: RECV op completed without a buffer on fd %d.\n", conn->sock_.get_fd());
      }

      int bytes_received = cqe.res;
      int buffer_id = cqe.flags >> IORING_CQE_BUFFER_SHIFT;
      uint8_t* buffer = conn->buffer_pool_.get_buffer_address(buffer_id);


      //printf("Received %d bytes on fd %d (buffer %d)\n", bytes_received, conn->sock_.get_fd(), buffer_id);

      conn->buffer_pool_.reprovide_buffer(buffer_id);

      // If multishot receive stops, we need to re-arm it.
      if (!(cqe.flags & IORING_CQE_F_MORE))
      {
        //printf("Multishot recv ended for fd %d, re-arming.\n", conn->sock_.get_fd());
        conn->new_multishot_recv_op();
      }
    }

    void new_multishot_recv_op()
    {
      auto& sqe = io_ctx_.create_request(recv_req_data_);
      sqe.flags |= IOSQE_BUFFER_SELECT;
      sqe.buf_group = buffer_pool_.get_group_id();
      io_uring_prep_recv_multishot(&sqe, sock_.get_fd(), NULL, 0, 0);
    }

    io_context& io_ctx_;
    bsd::socket sock_;
    provided_buffer_pool& buffer_pool_;
    io_context::req_data recv_req_data_;
    bool is_closed_;
  };
}