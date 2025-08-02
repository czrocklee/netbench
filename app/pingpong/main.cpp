#include "worker.hpp"
#include "../common/metadata.hpp"
#include "../common/utils.hpp"
#include <utility/metric_hud.hpp>

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

namespace
{
  std::atomic<bool> shutdown_flag = false;

  void signal_handler(int /* signum */)
  {
    shutdown_flag.store(true, std::memory_order::relaxed);
  }
}

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP pingpong"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:8080");

  bool initiator = false;
  app.add_flag("-i,--initiator", initiator, "Run as initiator");

  std::size_t msg_size;
  app.add_option("-z,--msg-size", msg_size, "Message size in bytes")->default_val(1024);

  worker::config cfg;
  app.add_option("-s,--buffer-size", cfg.buffer_size, "Size of receive buffer in bytes")->default_val(4096);

  int cpu_affinity;
  app.add_option("-c,--cpu-affinity", cpu_affinity, "cpu affinity for the io_context thread")->default_val(-1);

#ifdef IO_URING_API
  app.add_option("-b,--buffer-count", cfg.buffer_count, "Number of buffers in pool prepared for each worker")
    ->default_val(2048);

  int sqpoll_cpu_affinity;
  app.add_option("-k,--sqpoll-cpu-affinity", sqpoll_cpu_affinity, "cpu affinity for the kernel polling thread")
    ->default_val(-1);

  app.add_option("-d,--uring-depth", cfg.uring_depth, "io_uring queue depth")->default_val(512);
#endif
  CLI11_PARSE(app, argc, argv);

  ::signal(SIGINT, signal_handler);
  ::signal(SIGTERM, signal_handler);

  try
  {
    std::string host, port;
    parse_address(address_str, host, port);

    if (cpu_affinity >= 0)
    {
      set_thread_cpu_affinity(cpu_affinity);
      std::cout << "cpu thread affinity set to " << cpu_affinity << std::endl;
    }

#ifdef IO_URING_API
    io_uring_params params{};
    params.flags |= IORING_SETUP_SQPOLL;
    params.flags |= IORING_SETUP_SINGLE_ISSUER;
    //params.flags |= (IORING_SETUP_COOP_TASKRUN); //| IORING_SETUP_TASKRUN_FLAG);

    if (sqpoll_cpu_affinity >= 0)
    {
      params.flags |= IORING_SETUP_SQ_AFF;
      params.sq_thread_cpu = sqpoll_cpu_affinity;
    }

    params.sq_thread_idle = 0;

    cfg.params = params;
#endif

    auto pingponger = worker{cfg};

    if (initiator)
    {
      std::cout << "Running as initiator, connecting to " << address_str << std::endl;

      auto connector = net::connector{pingponger.get_io_context()};
      auto sock = connector.connect(host, port);

      metadata const md{.msg_size = msg_size};
      sock.send(::asio::buffer(&md, sizeof(md)), 0);
      sock.non_blocking(true);

      pingponger.add_connection(std::move(sock), msg_size);
      pingponger.send_initial_message();

      utility::metric_hud hud{std::chrono::seconds{5}};

      std::thread t{[&] {
        while (!shutdown_flag.load(std::memory_order::relaxed))
        {

          if (auto& queue = pingponger.get_sample_queue(); queue.read_available() > 0)
          {
            //    std::cout << queue.read_available()  << std::endl;

            auto const now = std::chrono::steady_clock::now();
            queue.consume_all([&](utility::sample const& sample) { hud.collect(sample, now); });
          }
        }
      }};

      pingponger.run(shutdown_flag);
      t.join();
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

      // while (true) {};
      pingponger.run(shutdown_flag);
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Pingpong has shut down gracefully." << std::endl;
  return 0;
}
