#include "receiver.hpp"
#include "acceptor.hpp"
#include "../metric_hud.hpp"

#include <CLI/CLI.hpp>
#include <atomic>
#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <future>

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
  app.add_option("-s,--buffer-size", buffer_size, "Size of each receive buffer in bytes")->default_val(1024);

  unsigned buffer_count;
  app.add_option("-c,--buffer-count", buffer_count, "Number of buffers in pool prepared for each receiver")->default_val(2048);


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

    //io_uring_params params{};
    //params.flags = IORING_SETUP_ATTACH_WQ;
    //params.wq_fd = io_ctx.get_ring_fd();

    // io_uring_params params{};
    // params.flags |= IORING_SETUP_SINGLE_ISSUER;

    for (auto i = 0u; i < num_threads; ++i)
    {
      //io_uring_params params{};
      //params.cq_entries = 65536;
      //params.flags == IORING_SETUP_DEFER_TASKRUN;
      //params.flags |= IORING_SETUP_COOP_TASKRUN;

      uring::receiver::config cfg = {
        .uring_depth = uring_depth,
        .buffer_count = buffer_count,
        .buffer_size = buffer_size,
        .buffer_group_id = static_cast<std::uint16_t>(i)};

      cfg.params.cq_entries = 65536;
      cfg.params.flags |= IORING_SETUP_R_DISABLED;
      cfg.params.flags |= IORING_SETUP_SINGLE_ISSUER;
      cfg.params.flags |= IORING_SETUP_DEFER_TASKRUN;
      cfg.params.flags |= IORING_SETUP_COOP_TASKRUN;

      

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

    auto collect_metric = [&receivers]() -> metric_hud::metric { // This lambda is good, keep it.
      std::vector<std::future<metric_hud::metric>> futures;
      futures.reserve(receivers.size());

      for (auto& receiver_ptr : receivers)
      {
        auto p = std::make_shared<std::promise<metric_hud::metric>>();
        futures.emplace_back(p->get_future()); // std::future requires <future>

        receiver_ptr->post([p, receiver = receiver_ptr.get()]() {
          p->set_value(receiver->get_metrics());
        }); // std::function requires <functional>
      }

      metric_hud::metric total_metric{};

      for (auto& f : futures)
      {
        auto m = f.get();
        total_metric.msgs += m.msgs;
        total_metric.bytes += m.bytes;
      }

      return total_metric;
    };

    metric_hud hud{std::chrono::seconds{5}, collect_metric};

    // The main thread now runs the acceptor's event loop until shutdown.
    while (!shutdown_flag)
    {
      io_ctx.run_for(std::chrono::milliseconds{100});
      hud.tick();
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