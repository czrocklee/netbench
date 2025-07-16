#include "worker.hpp"
#include "utility/metric_hud.hpp"

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

void signal_handler(int /* signum */)
{
  shutdown_flag = true;
}

namespace
{
  // Helper to split a "host:port" string into its components.
  void parse_address(std::string const& full_address, std::string& host, std::string& port)
  {
    auto colon_pos = full_address.find(':');

    if (colon_pos == std::string::npos) { throw std::runtime_error{"Invalid address format. Expected host:port"}; }

    host = full_address.substr(0, colon_pos);
    port = full_address.substr(colon_pos + 1);
  }
} // namespace

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP pingpong"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:8080");

  bool initiator = false;
  app.add_flag("-i,--initiator", initiator, "Run as initiator");

  worker::config cfg;
  app.add_option("-b,--buffer-size", cfg.buffer_size, "Size of receive buffer in bytes")->default_val(4096);

#ifdef IO_URING_API
  app.add_option("-c,--buffer-count", cfg.buffer_count, "Number of buffers in pool prepared for each worker")
    ->default_val(2048);

  app.add_option("-d,--uring-depth", cfg.uring_depth, "io_uring queue depth")->default_val(512);
#elifdef BSD_API
  app.add_option("-l,--read-limit", cfg.read_limit, "Optional read limit for BSD API (0 for no limit)")
    ->default_val(1024 * 64);
#endif

  bool busy_spin;
  app.add_option("-s,--busy-spin", busy_spin, "Enable busy spin polling")->default_val(false);

  CLI11_PARSE(app, argc, argv);

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try
  {
    std::string host, port;
    parse_address(address_str, host, port);

    net::io_context io_ctx{};

#ifdef IO_URING_API
    io_uring_params params{};
    params.cq_entries = 65536;
    params.flags |= IORING_SETUP_R_DISABLED;
    params.flags |= IORING_SETUP_SINGLE_ISSUER;
    params.flags |= IORING_SETUP_COOP_TASKRUN;
    cfg.params = params;
#endif

    worker pingpong_worker(cfg);
    pingpong_worker.start(busy_spin);

    if (initiator)
    {
        std::cout << "Running as initiator, connecting to " << address_str << std::endl;
        net::socket sock(io_ctx);
        sock.connect(host, port);
        pingpong_worker.post([&pingpong_worker, sock = std::move(sock)]() mutable {
            pingpong_worker.add_connection(std::move(sock));
            pingpong_worker.send_first_message();
        });
    }
    else
    {
        std::cout << "Running as acceptor, listening on " << address_str << std::endl;
        net::acceptor acceptor{io_ctx};
        acceptor.listen(host, port);
        acceptor.start([&](std::error_code ec, net::socket new_sock) {
            if (ec)
            {
                std::cerr << "Error accepting connection: " << ec.message() << std::endl;
                return;
            }
            pingpong_worker.post([&pingpong_worker, sock = std::move(new_sock)]() mutable {
                pingpong_worker.add_connection(std::move(sock));
            });
        });
    }


    auto collect_metric = [&]() -> utility::metric {
      utility::metric total_metric{};
      total_metric.init_histogram();
      std::promise<void> prom;
      auto fut = prom.get_future();
      pingpong_worker.post([&] {
          total_metric.add(pingpong_worker.get_metrics());
          prom.set_value();
      });
      fut.get();
      return total_metric;
    };

    utility::metric_hud hud{std::chrono::seconds{5}, collect_metric};

    while (!shutdown_flag)
    {
      io_ctx.run_for(std::chrono::milliseconds{1000});
      hud.tick();
#ifdef ASIO_API
      io_ctx.restart();
#endif
    }

    std::cout << "\nShutting down..." << std::endl;
    pingpong_worker.stop();
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Pingpong has shut down gracefully." << std::endl;
  return 0;
}
