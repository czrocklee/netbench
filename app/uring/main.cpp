#include "receiver.hpp"
#include "acceptor.hpp"

#include <CLI/CLI.hpp>
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

std::atomic<bool> shutdown_flag = false;

void signal_handler(int /* signum */) { shutdown_flag = true; }

namespace
{
  // Helper to split a "host:port" string into its components.
  void parse_address(const std::string& full_address, std::string& host, std::string& port)
  {
    auto colon_pos = full_address.find(':');

    if (colon_pos == std::string::npos)
    {
      throw std::runtime_error{"Invalid address format. Expected host:port"};
    }

    host = full_address.substr(0, colon_pos);
    port = full_address.substr(colon_pos + 1);
  }
} // namespace

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP io_uring Multi-threaded Echo Server"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:8080");

  unsigned buffer_size;
  app.add_option("-b,--buffer-size", buffer_size, "Size of each receive buffer in bytes")->default_val(1024);

  unsigned uring_depth;
  app.add_option("-d,--uring-depth", uring_depth, "io_uring queue depth")->default_val(512);

  unsigned num_threads;
  app.add_option("-t,--threads", num_threads, "Number of receiver threads to start")->default_val(1);

  CLI11_PARSE(app, argc, argv);

  if (num_threads <= 0)
  {
    std::cerr << "Number of threads must be greater than 0." << std::endl;
    return 1;
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try
  {
    std::string host, port;
    parse_address(address_str, host, port);

    std::vector<std::unique_ptr<uring::receiver>> receivers;
    std::cout << "Starting " << num_threads << " receiver threads on " << address_str << "..." << std::endl;

    uring::io_context io_ctx{128};

    io_uring_params params{};

    //params.flags = IORING_SETUP_ATTACH_WQ;
    //params.wq_fd = io_ctx.get_ring_fd();


    //io_uring_params params{};
    //params.flags |= IORING_SETUP_SINGLE_ISSUER;

    for (auto i = 0u; i < num_threads; ++i)
    {
      uring::receiver::config cfg = {
        .uring_depth = uring_depth,
        .buffer_count = 4096,
        .buffer_size = buffer_size,
        .buffer_group_id = static_cast<std::uint16_t>(i),
        .params = params};
      receivers.emplace_back(std::make_unique<uring::receiver>(cfg))->start();
    }

    auto on_accept = [&, next_receiver_idx = 0](bsd::socket&& new_sock) mutable {
      int accepted_fd = new_sock.get_fd();

      if (!receivers[next_receiver_idx]->add_connection(std::move(new_sock)))
      {
        std::cerr << "Main thread FAILED to hand off fd " << accepted_fd << " to receiver " << next_receiver_idx
                  << " (queue full?)" << std::endl;
        // The bsd::socket new_sock will be destructed here, closing the FD.
      }

      next_receiver_idx = (next_receiver_idx + 1) % receivers.size();
    };

    uring::acceptor acceptor{io_ctx, on_accept};
    acceptor.listen(host, port);
    acceptor.start();
    std::cout << "Main thread acceptor listening on " << address_str << std::endl;

    // The main thread now runs the acceptor's event loop until shutdown.
    while (!shutdown_flag) { 
      io_ctx.poll(); 
      //io_ctx.run_for(std::chrono::seconds{1}); }
    }
    std::cout << "\nShutting down..." << std::endl;

    for (auto& receiver : receivers) { receiver->stop(); }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Server has shut down gracefully." << std::endl;
  return 0;
}