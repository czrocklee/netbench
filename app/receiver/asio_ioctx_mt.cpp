#include "utils.hpp"
#include "metric_hud.hpp"
#include <utility/logger.hpp>
#include "metadata.hpp"

#include <rasio/tcp.hpp>
using net = rasio::tcp;

#include <CLI/CLI.hpp>
#include <asio/socket_base.hpp>
#include <asio/read.hpp>

#include <atomic>
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

namespace
{
  struct connection
  {
    net::receiver receiver;
    std::atomic<std::uint64_t> ops{0};
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<std::uint64_t> msgs{0};
    std::size_t msg_size{0};
    std::size_t partial_bytes{0};
    std::unique_ptr<std::byte[]> partial_buffer;
    std::size_t partial_buffer_size{0};
    metric metrics;

    connection(net::io_context& io_ctx, std::size_t buffer_size) : receiver(io_ctx, buffer_size) {}

    void on_data(::asio::const_buffer const data)
    {
      auto data_left = data;

      if (partial_buffer_size > 0)
      {
        auto addr = reinterpret_cast<std::byte const*>(data_left.data());
        auto size = std::min(msg_size - partial_buffer_size, data_left.size());
        std::memcpy(partial_buffer.get() + partial_buffer_size, addr, size);
        partial_buffer_size += size;

        if (partial_buffer_size == msg_size)
        {
          on_new_message(partial_buffer.get());
          partial_buffer_size = 0;
        }

        data_left += size;
      }

      while (data_left.size() > 0)
      {
        auto addr = reinterpret_cast<std::byte const*>(data_left.data());

        if (data_left.size() < msg_size)
        {
          std::memcpy(partial_buffer.get(), addr, data_left.size());
          partial_buffer_size = data_left.size();
          break;
        }

        on_new_message(addr);
        data_left += msg_size;
      }

      bytes.store(bytes.load(std::memory_order_relaxed) + data.size(), std::memory_order_relaxed);
      ops.store(ops.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    void on_new_message(void const* buffer)
    {
      msgs.store(msgs.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
  };
} // namespace

int main(int argc, char** argv)
{
  CLI::App app{"ASIO multi-threaded TCP receiver"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:19004");

  unsigned buffer_size;
  app.add_option("-b,--buffer-size", buffer_size, "Size of receive buffer in bytes")
    ->default_val(1024)
    ->transform(CLI::AsSizeValue(false));

  unsigned num_workers;
  app.add_option("-w,--workers", num_workers, "Number of worker threads to start")->default_val(1);

  // Optional: pin worker threads to specific CPUs (comma-separated list, index order)
  std::vector<int> worker_cpus; // size <= num_workers; others unpinned
  app.add_option("--worker-cpu-ids", worker_cpus, "Comma-separated CPU IDs for workers in index order (e.g., 0,2,4)")
    ->delimiter(',');

  int metric_hud_interval_secs;
  app
    .add_option(
      "-m,--metric-hud-interval-secs", metric_hud_interval_secs, "Metric HUD interval in seconds, 0 for disabled")
    ->default_val(5);

  std::string log_file;
  app.add_option("-l,--log-file", log_file, "Path to log file")->default_val("/tmp/receiver.log");

  std::string log_level;
  app.add_option("-L,--log-level", log_level, "Minimum log level (trace, debug, info, warn, error, critical)")
    ->default_val("info");

  // Optional socket buffer sizes (0 = system default)
  int so_rcvbuf;
  app.add_option("--so-rcvbuf", so_rcvbuf, "Socket receive buffer size in bytes (0 for system default)")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(false));

  int so_sndbuf;
  app.add_option("--so-sndbuf", so_sndbuf, "Socket send buffer size in bytes (0 for system default)")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(false));

  bool shutdown_on_disconnect;
  app.add_flag("-d,--shutdown-on-disconnect", shutdown_on_disconnect, "Shutdown server when all clients disconnect");

  int collect_latency_every_n_samples;
  app
    .add_option(
      "-c,--collect-latency-every-n-samples",
      collect_latency_every_n_samples,
      "Collect one latency sample every N messages (0 to disable)")
    ->default_val(0);

  // Results and tags
  std::string results_dir;
  app.add_option("-r,--results-dir", results_dir, "Directory to write run outputs (metadata.json, metrics.json)")
    ->default_val("");

  std::vector<std::string> tags;
  app.add_option("--tag", tags, "User tags (repeatable: --tag k=v)");

  CLI11_PARSE(app, argc, argv);

  if (collect_latency_every_n_samples > 0)
  {
    std::cerr << "Placeholder only, Latency collection is not supported in this ASIO multi-threaded receiver yet."
              << std::endl;
    return 1;
  }

  if (num_workers <= 0)
  {
    std::cerr << "Number of workers must be greater than 0." << std::endl;
    return 1;
  }

  try
  {
    // Setup logging
    utility::init_log_file(log_file);
    utility::set_log_level(utility::from_string(log_level));

    // Signal handling (Ctrl-C etc.)
    std::atomic<int>& shutdown_counter = setup_signal_handlers();

    std::string host, port;
    parse_address(address_str, host, port);

    std::list<connection> conns;
    std::mutex conns_lock;
    std::size_t closed_conns = 0;
    net::io_context io_ctx{static_cast<int>(num_workers)};

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

      metadata md{};
      ::asio::read(new_sock, ::asio::buffer(&md, sizeof(md)), ::asio::transfer_all());
      iter->msg_size = md.msg_size;
      iter->partial_buffer = std::make_unique<std::byte[]>(iter->msg_size);
      iter->metrics.begin_ts = std::chrono::steady_clock::now();

      if (so_rcvbuf > 0)
      {
        new_sock.set_option(::asio::socket_base::receive_buffer_size{so_rcvbuf});
      }

      if (so_sndbuf > 0)
      {
        new_sock.set_option(::asio::socket_base::send_buffer_size{so_sndbuf});
      }

      iter->receiver.open(std::move(new_sock));
      iter->receiver.start([&, iter](std::error_code ec, ::asio::const_buffer data) {
        if (ec)
        {
          iter->metrics.end_ts = std::chrono::steady_clock::now();

          if (!shutdown_on_disconnect)
          {
            ::asio::post(io_ctx, [&, iter] {
              std::lock_guard<std::mutex> lg{conns_lock};
              conns.erase(iter);
            });

            return;
          }

          if (std::lock_guard<std::mutex> lg{conns_lock}; ++closed_conns == conns.size())
          {
            shutdown_counter = 0;
          }

          return;
        }

        iter->on_data(data);
      });
    });

    std::cout << "Main thread acceptor listening on " << address_str << std::endl;

    std::vector<std::jthread> workers;
    workers.reserve(num_workers);

    for (unsigned i = 0; i < num_workers; ++i)
    {
      int cpu_id = (i < worker_cpus.size() ? worker_cpus[i] : -1);

      workers.emplace_back([&io_ctx, cpu_id] {
        if (cpu_id >= 0)
        {
          set_thread_cpu_affinity(cpu_id);
        }

        io_ctx.run();
      });
    }

    auto metric_hud = setup_metric_hud(std::chrono::seconds{metric_hud_interval_secs}, [&] {
      metric total_metric{};

      {
        std::lock_guard<std::mutex> lg{conns_lock};

        for (auto const& conn : conns)
        {
          total_metric.ops += conn.ops.load(std::memory_order_relaxed);
          total_metric.msgs += conn.msgs.load(std::memory_order_relaxed);
          total_metric.bytes += conn.bytes.load(std::memory_order_relaxed);
        }
      }

      return total_metric;
    });

    while (shutdown_counter > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds{1000});

      if (metric_hud)
      {
        metric_hud->tick();
      }
    }

    if (shutdown_counter == 0)
    {
      std::cout << "All clients disconnected, server stopped." << std::endl;
    }
    else
    {
      std::cout << "\nShutdown signal received, stopping server..." << std::endl;
    }

    io_ctx.stop();

    for (auto& worker : workers)
    {
      if (worker.joinable())
      {
        worker.join();
      }
    }

    if (!results_dir.empty())
    {
      auto const dir = std::filesystem::path{results_dir};

      if (!std::filesystem::exists(dir))
      {
        std::filesystem::create_directories(dir);
      }

      dump_run_metadata(dir, std::vector<std::string>{argv, argv + argc}, tags);

      std::vector<metric const*> all_metrics;

      for (auto& conn : conns)
      {
        conn.metrics.ops = conn.ops.load(std::memory_order_relaxed);
        conn.metrics.msgs = conn.msgs.load(std::memory_order_relaxed);
        conn.metrics.bytes = conn.bytes.load(std::memory_order_relaxed);
        all_metrics.push_back(&conn.metrics);
      }

      dump_metrics(dir, all_metrics);
    }

    std::cout << "Server has shut down gracefully." << std::endl;
    return 0;
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}