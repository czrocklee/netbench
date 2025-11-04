#include "worker.hpp"
#include "metadata.hpp"
#include "utils.hpp"
#include "metric_hud.hpp"

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
#include <optional>
#include <filesystem>

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP pingpong"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:8080");

  bool initiator = false;
  app.add_flag("-i,--initiator", initiator, "Run as initiator");

  std::size_t msg_size;
  app.add_option("-z,--msg-size", msg_size, "Message size in bytes")
    ->default_val(1024)
    ->transform(CLI::AsSizeValue(false));

  worker::config cfg;
  app.add_option("-s,--buffer-size", cfg.buffer_size, "Size of receive buffer in bytes")
    ->default_val(4096)
    ->transform(CLI::AsSizeValue(false));

  // Warmup count and stopping options
  std::uint64_t warmup_count = 10000;
  app.add_option("-w,--warmup-count", cfg.warmup_count, "Number of initial ping-pong messages to ignore for stats")
    ->default_val(10000);

  std::uint64_t max_samples = 0; // 0 = run until stopped via signal
  app.add_option("--max-samples", max_samples, "Stop after collecting this many RTT samples (post-warmup)")
    ->default_val(0);

  int duration_secs = 0; // 0 = disabled
  app.add_option("-t,--duration-secs", duration_secs, "Stop after this many seconds")->default_val(0);

  int metric_hud_interval_secs = 5; // 0 disables HUD
  app
    .add_option(
      "-m,--metric-hud-interval-secs", metric_hud_interval_secs, "Metric HUD interval in seconds, 0 to disable")
    ->default_val(5);

  std::string results_dir; // optional output dir for initiator to dump histogram
  app.add_option("-r,--results-dir", results_dir, "Directory to write histogram and metrics (initiator only)")
    ->default_val("");

  std::vector<std::string> tags;
  app.add_option("--tag", tags, "User tags (repeatable: --tag k=v)");

  int cpu_affinity;
  app.add_option("-c,--cpu-id", cpu_affinity, "cpu affinity for the io_context thread")->default_val(-1);

#ifdef IO_URING_API
  app.add_option("-b,--buffer-count", cfg.buffer_count, "Number of buffers in pool prepared for each worker")
    ->default_val(2048);

  bool enable_sqpoll = true;
  app.add_flag("--sqpoll,!--no-sqpoll", enable_sqpoll, "Enable io_uring SQPOLL mode (default: on)");

  // Enable io_uring zerocopy sends for pingpong replies and initiator sends
  app.add_flag("--zerocopy", cfg.zerocopy, "Enable io_uring MSG_ZEROCOPY for sends (uring only)");

  int sqpoll_cpu_affinity;
  app.add_option("-k,--sqpoll-cpu-id", sqpoll_cpu_affinity, "cpu affinity for the kernel polling thread")
    ->default_val(-1);

  app.add_option("--sq-entries", cfg.sq_entries, "io_uring SQ entries")->default_val(512);
#endif
  CLI11_PARSE(app, argc, argv);

  std::atomic<int>& shutdown_counter = setup_signal_handlers();

  try
  {
    std::string host, port;
    parse_address(address_str, host, port);

#ifdef IO_URING_API
    io_uring_params params{};
    params.flags |= IORING_SETUP_SINGLE_ISSUER;

    if (enable_sqpoll)
    {
      params.flags |= IORING_SETUP_SQPOLL;
      if (sqpoll_cpu_affinity >= 0)
      {
        params.flags |= IORING_SETUP_SQ_AFF;
        params.sq_thread_cpu = sqpoll_cpu_affinity;
      }
      // 0 means wake immediately when idle timer expires; keep current behavior
      params.sq_thread_idle = 0;
    }

    cfg.params = params;
#endif

    auto pingponger = worker{cfg};

    if (initiator)
    {
      std::cout << "Running as initiator, connecting to " << address_str << std::endl;

      auto connector = net::connector{pingponger.get_io_context()};
      auto sock = connector.connect(host, port);

      auto const md = metadata{.msg_size = msg_size};
      sock.send(::asio::buffer(&md, sizeof(md)), 0);

      pingponger.add_connection(std::move(sock), msg_size);
      pingponger.send_initial_message();

      auto total_metric = metric{};
      total_metric.init_histogram();
      auto metric_hud = setup_metric_hud(std::chrono::seconds{metric_hud_interval_secs}, [&total_metric] {
        auto clone = metric{};
        clone.init_histogram();
        clone.add(total_metric);
        return clone;
      });

      std::thread t{[&] {
        total_metric.begin_ts = std::chrono::steady_clock::now();

        while (shutdown_counter.load(std::memory_order::relaxed) > 0)
        {
          if (auto& queue = pingponger.get_sample_queue(); queue.read_available() > 0)
          {
            auto const now = boost::chrono::process_cpu_clock::now();
            queue.consume_all([&](sample const& s) {
              // HUD view (optional)
              if (metric_hud)
              {
                metric_hud->tick();
              }

              auto const latency = s.recv_ts - s.send_ts;
              ++total_metric.msgs;
              total_metric.update_latency_histogram(latency);

              if (max_samples > 0 && total_metric.msgs >= max_samples)
              {
                shutdown_counter.store(0, std::memory_order::relaxed);
              }
            });
          }

          if (
            duration_secs > 0 &&
            std::chrono::steady_clock::now() - total_metric.begin_ts >= std::chrono::seconds{duration_secs})
          {
            shutdown_counter.store(0, std::memory_order::relaxed);
          }
        }

        std::cout << "Sample collection thread exiting\n";

        total_metric.end_ts = std::chrono::steady_clock::now();
      }};

      if (cpu_affinity >= 0)
      {
        set_thread_cpu_affinity(cpu_affinity);
        std::cout << "cpu thread affinity set to " << cpu_affinity << std::endl;
      }

      pingponger.run(shutdown_counter);
      t.join();

      total_metric.ops = total_metric.msgs;              // 1 RTT counted as 1 op
      total_metric.bytes = total_metric.msgs * msg_size; // approximate bytes accounted on initiator

      if (!results_dir.empty())
      {
        std::filesystem::create_directories(results_dir);
        dump_metrics(results_dir, std::vector<metric const*>{&total_metric});
      }
    }
    else
    {
      std::cout << "Running as acceptor, listening on " << address_str << std::endl;
      net::acceptor acceptor{pingponger.get_io_context()};
      acceptor.listen(host, port);
      acceptor.start([&](std::error_code ec, net::socket new_sock) {
        if (ec)
        {
          std::cerr << "Error accepting connection: " << ec.message() << std::endl;
          return;
        }

        metadata md{};
        new_sock.receive(::asio::buffer(&md, sizeof(md)), 0);
        pingponger.add_connection(std::move(new_sock), md.msg_size);
      });

      if (cpu_affinity >= 0)
      {
        set_thread_cpu_affinity(cpu_affinity);
        std::cout << "cpu thread affinity set to " << cpu_affinity << std::endl;
      }

      pingponger.run(shutdown_counter);
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Pingpong has shut down gracefully." << std::endl;

  // Initiator results: write metadata to match receiver directory structure if requested
  if (initiator && !results_dir.empty())
  {
    try
    {
      std::filesystem::create_directories(results_dir);
      dump_run_metadata(std::filesystem::path{results_dir}, std::vector<std::string>{argv, argv + argc}, tags);
    }
    catch (std::exception const& e)
    {
      std::cerr << "Failed to write metadata.json: " << e.what() << std::endl;
    }
  }
  return 0;
}
