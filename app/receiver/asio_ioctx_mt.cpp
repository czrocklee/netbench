#include "utility/metric_hud.hpp"

#include "rasio/tcp.hpp"
using net = rasio::tcp;

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
#include <list>
#include <mutex>
#include <atomic>

std::atomic<bool> shutdown_flag = false;

void signal_handler(int /* signum */) { shutdown_flag = true; }

namespace
{
  // Helper to split a "host:port" string into its components.
  void parse_address(std::string const& full_address, std::string& host, std::string& port)
  {
    auto colon_pos = full_address.find(':');

    if (colon_pos == std::string::npos)
    {
      throw std::runtime_error{"Invalid address format. Expected host:port"};
    }

    host = full_address.substr(0, colon_pos);
    port = full_address.substr(colon_pos + 1);
  }

  struct connection
  {
    net::receiver receiver;
    std::atomic<std::uint64_t> ops{0};
    std::atomic<std::uint64_t> bytes{0};

    connection(net::io_context& io_ctx, std::size_t buffer_size) : receiver(io_ctx, buffer_size) {}
  };
} // namespace

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP io_uring Multi-threaded Echo Server"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:8080");

  unsigned buffer_size;
  app.add_option("-s,--buffer-size", buffer_size, "Size of each receive buffer in bytes")->default_val(1024);

  unsigned num_workers;
  app.add_option("-w,--workers", num_workers, "Number of worker threads to start")->default_val(1);

  CLI11_PARSE(app, argc, argv);

  if (num_workers <= 0)
  {
    std::cerr << "Number of workers must be greater than 0." << std::endl;
    return 1;
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try
  {
    std::string host, port;
    parse_address(address_str, host, port);

    std::list<connection> conns;
    std::mutex conns_lock;
    std::atomic<std::uint64_t> total_ops_received{0};
    std::atomic<std::uint64_t> total_bytes_received{0};

    net::io_context io_ctx{};

    net::acceptor acceptor{io_ctx};
    acceptor.listen(host, port);
    acceptor.start([&](std::error_code ec, net::socket new_sock) mutable {
      if (ec)
      {
        std::cerr << "Error accepting connection: " << ec.message() << std::endl;
        return;
      }

      std::list<connection>::iterator iter;

      {
        std::lock_guard<std::mutex> lg{conns_lock};
        iter = conns.emplace(conns.begin(), io_ctx, buffer_size);
      }

      iter->receiver.open(std::move(new_sock));
      iter->receiver.start([&, iter](std::error_code ec, ::asio::const_buffer data) {
        if (ec)
        {
          std::cerr << "Error receiving data: " << ec.message() << std::endl;
          std::lock_guard<std::mutex> lg{conns_lock};
          conns.erase(iter);
          return;
        }

        std::size_t bytes_received = data.size();
        iter->ops.store(iter->ops.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        iter->bytes.store(iter->bytes.load(std::memory_order_relaxed) + bytes_received, std::memory_order_relaxed);
      });
    });

    std::cout << "Main thread acceptor listening on " << address_str << std::endl;

    std::vector<std::jthread> workers;
    workers.reserve(num_workers);

    for (unsigned i = 0; i < num_workers; ++i)
    {
      workers.emplace_back([&io_ctx]() { io_ctx.run(); });
    }

    auto collect_metric = [&conns, &conns_lock] {
      utility::metric total_metric{};

      std::lock_guard<std::mutex> lg{conns_lock};
      for (auto const& conn : conns)
      {
        total_metric.ops += conn.ops.load(std::memory_order_relaxed);
        total_metric.bytes += conn.bytes.load(std::memory_order_relaxed);
      }

      return total_metric;
    };

    utility::metric_hud hud{std::chrono::seconds{5}, collect_metric};

    while (!shutdown_flag)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds{1000});
      hud.tick();
    }

    std::cout << "\nShutting down..." << std::endl;

    io_ctx.stop();
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Server has shut down gracefully." << std::endl;
  return 0;
}