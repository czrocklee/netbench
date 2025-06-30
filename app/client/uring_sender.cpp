#include "uring_sender.hpp"

#include <iostream>
#include <numeric>

namespace client
{

  uring_sender::uring_sender(int id, const std::string& host, const std::string& port, int msg_size, int msgs_per_sec)
    : id_{id},
      conn_{AF_INET, SOCK_STREAM, 0},
      interval_(
        msgs_per_sec > 0 ? std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec
                         : std::chrono::nanoseconds{0})
  {
    // Initialize the io_uring instance directly.
    if (::io_uring_queue_init(256, &ring_, 0) < 0)

//    if (::io_uring_queue_init(32768, &ring_, 0) < 0)
    {
      throw std::runtime_error("io_uring_queue_init failed: " + std::string(strerror(errno)));
    }

    if (::posix_memalign((void**)&iovec_.iov_base, 4096, msg_size))
    {
      throw std::runtime_error("posix_memalign failed: " + std::string(strerror(errno)));
    }

    iovec_.iov_len = msg_size;


    if (int ret = ::io_uring_register_buffers(&ring_, &iovec_, 1); ret != 0)
    {
      throw std::runtime_error("io_uring_register_buffers failed: " + std::string(strerror(-ret)));
    }


    // message_.resize(msg_size, std::byte{'a'});

    // Establish the connection.
    conn_.connect(host, port);

    // Register the file descriptor with this io_uring instance.
    int fd = conn_.get_fd();

    if (io_uring_register_files(&ring_, &fd, 1) < 0)
    {
      io_uring_queue_exit(&ring_); // Clean up the ring on failure.
      throw std::runtime_error("io_uring_register_files failed: " + std::string(strerror(errno)));
    }
  }

  uring_sender::~uring_sender()
  {
    // Unregister files and shut down the io_uring instance.
    io_uring_unregister_files(&ring_);
    io_uring_queue_exit(&ring_);
  }

  void uring_sender::start()
  {
    thread_ = std::jthread{[this] { run(); }};
  }

  uint64_t uring_sender::total_msgs_sent() const { return total_msgs_completed_.load(std::memory_order_relaxed); }

  uint64_t uring_sender::total_bytes_sent() const { return total_bytes_sent_.load(std::memory_order_relaxed); }

  void uring_sender::run()
  {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t submitted_count = 0;
    const unsigned cqe_batch_size = 64; // Number of completions to process at once.

    while (true)
    {
      // Determine how many messages should have been sent by now based on the rate.
      uint64_t expected_sends = submitted_count + 1; // Default to sending as fast as possible.
      if (interval_.count() > 0)
      {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        expected_sends = elapsed / interval_;
      }

      // Submit new send requests if we are behind schedule.
      unsigned submitted_this_loop = 0;
      while (submitted_count < expected_sends)
      {
        ::io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe)
        {
          // Submission queue is full, break to submit and reap completions.
          //std::cout << "ops...." << std::endl; 
          break;

        }

        io_uring_prep_send_zc_fixed(sqe, 0, iovec_.iov_base, iovec_.iov_len, 0, 0, 0);
        sqe->flags |= IOSQE_FIXED_FILE; // Use the registered file descriptor.
        io_uring_sqe_set_data(sqe, nullptr);
        std::cout << "sent " << iovec_.iov_len << std::endl;

        submitted_count++;
        submitted_this_loop++;
      }

      // Submit the prepared requests to the kernel.
      if (submitted_this_loop > 0)
      {
        io_uring_submit(&ring_);
      }

      // Efficiently process a batch of completions.
      io_uring_cqe* cqe_array[cqe_batch_size];
      int completions = io_uring_peek_batch_cqe(&ring_, cqe_array, cqe_batch_size);

      if (completions > 0)
      {
        for (int i = 0; i < completions; ++i) { on_send_complete(cqe_array[i]); }
        io_uring_cq_advance(&ring_, completions);
      }
    }
  }

  void uring_sender::on_send_complete(io_uring_cqe* cqe)
  {
    if (cqe->res < 0)
    {
      /* if (cqe->res < iovec_.iov_len) */ { std::cout << "short write " << cqe->res << std::endl; }

      if (cqe->res != -EAGAIN && cqe->res != -ECONNRESET && cqe->res != -EPIPE)
      {
        std::cerr << "Sender " << id_ << " send error: " << strerror(-cqe->res) << std::endl;
      }
    }
    else
    {
      std::cout << "writen " << cqe->res << std::endl;
      total_bytes_sent_ += cqe->res;
      total_msgs_completed_++;
    }
  }
}