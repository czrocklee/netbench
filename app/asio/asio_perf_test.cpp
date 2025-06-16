// =====================================================================
// Boost.Asio TCP Echo Performance Test
// =====================================================================
// This file contains two applications:
// 1. A simple TCP echo server that echoes back received data.
// 2. A TCP client that measures throughput to the echo server.
//
// To compile (requires Boost library):
// g++ -std=c++17 -o echo_server echo_performance_test.cpp -DBOOST_ASIO_STANDALONE -lboost_system -pthread
// g++ -std=c++17 -o echo_client echo_performance_test.cpp -DBOOST_ASIO_STANDALONE -lboost_system -pthread
//
// To run:
// 1. Start the server: ./echo_server <port>
//    Example: ./echo_server 8080
// 2. Start the client: ./echo_client <server_ip> <port> <data_size_mb> <iterations>
//    Example: ./echo_client 127.0.0.1 8080 100 5
//
// Note: BOOST_ASIO_STANDALONE is used to avoid linking with all of Boost
// if you only want to use Boost.Asio. If you have the full Boost installed,
// you can remove this define and just link against -lboost_system.
// =====================================================================

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <numeric>
#include <iomanip>
#include <functional>

// Use standalone Boost.Asio if defined, otherwise use full Boost
// #ifdef BOOST_ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
/* #else
#include <boost/asio.hpp>
#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#endif */

// =====================================================================
// SERVER IMPLEMENTATION
// =====================================================================

namespace server
{
  using tcp = ::asio::ip::tcp;
  // using asio::ip::tcp;

  // Represents a single client session
  class session : public std::enable_shared_from_this<session>
  {
  public:
    session(tcp::socket socket) : socket_(std::move(socket)) {}

    void start()
    {
      do_read(); // Start reading data from the client
    }

  private:
    void do_read()
    {
      auto self(shared_from_this());
      socket_.async_read_some(asio::buffer(data_, max_length),
                              [this, self](const asio::error_code &error, size_t bytes_transferred)
                              {
                                if (!error)
                                {
                                  do_write(bytes_transferred); // Echo back the received data
                                }
                                else if (error == asio::error::eof || error == asio::error::connection_reset)
                                {
                                  // Client disconnected gracefully or forcefully
                                  std::cout << "Client disconnected." << std::endl;
                                }
                                else
                                {
                                  std::cerr << "Read error: " << error.message() << std::endl;
                                }
                              });
    }

    void do_write(size_t length)
    {
      auto self(shared_from_this());
      asio::async_write(socket_, asio::buffer(data_, length),
                        [this, self](const asio::error_code &error, size_t bytes_transferred)
                        {
                          if (!error)
                          {
                            do_read(); // Continue reading for more data from the same client
                          }
                          else
                          {
                            std::cerr << "Write error: " << error.message() << std::endl;
                          }
                        });
    }

    tcp::socket socket_;
    enum
    {
      max_length = 8192
    }; // Max buffer size for reading/writing
    char data_[max_length];
  };

  // Manages accepting new client connections
  class server
  {
  public:
    server(asio::io_context &io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    {
      do_accept(); // Start accepting connections
    }

  private:
    void do_accept()
    {
      // Create a new socket for the incoming connection
      acceptor_.async_accept(
          [this](const asio::error_code &error, tcp::socket socket)
          {
            if (!error)
            {
              // On successful connection, create a new session and start it
              std::cout << "Accepted connection from: " << socket.remote_endpoint() << std::endl;
              std::make_shared<session>(std::move(socket))->start();
            }
            else
            {
              std::cerr << "Accept error: " << error.message() << std::endl;
            }
            do_accept(); // Continue accepting new connections
          });
    }

    tcp::acceptor acceptor_;
  };

} // namespace server

// =====================================================================
// CLIENT IMPLEMENTATION
// =====================================================================

namespace client
{

  using asio::ip::tcp;

  class echo_client
  {
  public:
    echo_client(asio::io_context &io_context,
                const tcp::resolver::results_type &endpoints,
                size_t send_size_bytes,
                int iterations)
        : io_context_(io_context),
          socket_(io_context),
          send_buffer_(send_size_bytes),
          receive_buffer_(send_size_bytes),
          total_bytes_sent_(0),
          total_bytes_received_(0),
          iterations_left_(iterations),
          send_start_time_(std::chrono::high_resolution_clock::now()),
          receive_start_time_(std::chrono::high_resolution_clock::now())
    {
      // Initialize send buffer with some data (e.g., 'a' characters)
      std::fill(send_buffer_.begin(), send_buffer_.end(), 'a');
      do_connect(endpoints); // Start connection process
    }

  private:
    void do_connect(const tcp::resolver::results_type &endpoints)
    {
      asio::async_connect(socket_, endpoints,
                          [this](const asio::error_code &error, const tcp::endpoint & /*endpoint*/)
                          {
                            if (!error)
                            {
                              std::cout << "Connected to server." << std::endl;
                              send_start_time_ = std::chrono::high_resolution_clock::now(); // Start timer
                              do_write();                                                   // Start sending data
                            }
                            else
                            {
                              std::cerr << "Connect error: " << error.message() << std::endl;
                              io_context_.stop(); // Stop io_context if connection fails
                            }
                          });
    }

    void do_write()
    {
      if (iterations_left_ > 0)
      {
        asio::async_write(socket_, asio::buffer(send_buffer_),
                          [this](const asio::error_code &error, size_t bytes_transferred)
                          {
                            if (!error)
                            {
                              total_bytes_sent_ += bytes_transferred;
                              receive_start_time_ = std::chrono::high_resolution_clock::now(); // Start receive timer for this chunk
                              do_read();                                                       // After sending, expect to receive the echo
                            }
                            else
                            {
                              std::cerr << "Write error: " << error.message() << std::endl;
                              io_context_.stop();
                            }
                          });
      }
      else
      {
        // All iterations completed
        socket_.shutdown(tcp::socket::shutdown_both); // Gracefully close connection
        socket_.close();
        io_context_.stop();
      }
    }

    void do_read()
    {
      asio::async_read(socket_, asio::buffer(receive_buffer_),
                       [this](const asio::error_code &error, size_t bytes_transferred)
                       {
                         if (!error)
                         {
                           total_bytes_received_ += bytes_transferred;
                           // Check if the received data matches the sent data (optional but good for echo test)
                           // if (std::memcmp(send_buffer_.data(), receive_buffer_.data(), bytes_transferred) != 0) {
                           //     std::cerr << "Data mismatch!" << std::endl;
                           // }

                           iterations_left_--;
                           if (iterations_left_ > 0)
                           {
                             // Continue with next iteration
                             do_write();
                           }
                           else
                           {
                             // All iterations complete, stop the io_context
                             io_context_.stop();
                           }
                         }
                         else
                         {
                           std::cerr << "Read error: " << error.message() << std::endl;
                           io_context_.stop();
                         }
                       });
    }

  public:
    asio::io_context &io_context_;
    tcp::socket socket_;
    std::vector<char> send_buffer_;
    std::vector<char> receive_buffer_;
    size_t total_bytes_sent_;
    size_t total_bytes_received_;
    int iterations_left_;

    std::chrono::high_resolution_clock::time_point send_start_time_;
    std::chrono::high_resolution_clock::time_point receive_start_time_;
  };

} // namespace client

// =====================================================================
// MAIN ENTRY POINT
// =====================================================================

int main(int argc, char *argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage for server: " << argv[0] << " <port>" << std::endl;
      std::cerr << "Usage for client: " << argv[0] << " <host> <port> <data_size_mb> <iterations>" << std::endl;
      return 1;
    }

    // Determine if running as server or client based on argument count
    if (argc == 2)
    { // Server mode
      short port = static_cast<short>(std::atoi(argv[1]));
      asio::io_context io_context;
      server::server s(io_context, port);
      std::cout << "Echo Server started on port " << port << std::endl;
      io_context.run(); // Run the io_context, blocking until work is done
    }
    else if (argc == 5)
    { // Client mode
      std::string host = argv[1];
      std::string port = argv[2];
      size_t data_size_mb = static_cast<size_t>(std::atoi(argv[3]));
      int iterations = std::atoi(argv[4]);

      if (data_size_mb == 0 || iterations == 0)
      {
        std::cerr << "Data size and iterations must be greater than 0." << std::endl;
        return 1;
      }

      size_t send_size_bytes = data_size_mb * 1024 * 1024; // Convert MB to bytes

      asio::io_context io_context;
      asio::ip::tcp::resolver resolver(io_context);
      asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, port);

      std::cout << "Starting client with:" << std::endl;
      std::cout << "  Host: " << host << std::endl;
      std::cout << "  Port: " << port << std::endl;
      std::cout << "  Data per iteration: " << data_size_mb << " MB" << std::endl;
      std::cout << "  Total iterations: " << iterations << std::endl;
      std::cout << "  Total data to send (approx): " << (double)data_size_mb * iterations << " MB" << std::endl;

      client::echo_client c(io_context, endpoints, send_size_bytes, iterations);

      // Run the io_context in a separate thread to allow main thread to wait for it
      std::thread io_thread([&io_context]()
                            { io_context.run(); });

      // Wait for the io_context to complete all work (client finishes)
      io_thread.join();

      // Calculate and display results
      double total_data_transfer_gb = (double)c.total_bytes_sent_ * 2 / (1024.0 * 1024.0 * 1024.0);                           // Sent + Received
      auto duration_send = std::chrono::duration_cast<std::chrono::milliseconds>(c.receive_start_time_ - c.send_start_time_); // Duration from start of send to end of last receive
      double total_time_seconds = duration_send.count() / 1000.0;

      if (total_time_seconds > 0)
      {
        double throughput_mbps_send = (double)c.total_bytes_sent_ / (1024.0 * 1024.0) / total_time_seconds; // MB/s (send only)
        double throughput_mbps_total = total_data_transfer_gb * 1024 / total_time_seconds;                  // MB/s (send + receive)
        double throughput_gbps_total = total_data_transfer_gb * 8 / total_time_seconds;                     // Gbps (send + receive)

        std::cout << "\n--- Performance Results ---" << std::endl;
        std::cout << "Total sent data: " << c.total_bytes_sent_ / (1024.0 * 1024.0) << " MB" << std::endl;
        std::cout << "Total received data: " << c.total_bytes_received_ / (1024.0 * 1024.0) << " MB" << std::endl;
        std::cout << "Total data transferred (duplex): " << total_data_transfer_gb << " GB" << std::endl;
        std::cout << "Total time taken: " << std::fixed << std::setprecision(3) << total_time_seconds << " seconds" << std::endl;
        std::cout << "Throughput (send only): " << std::fixed << std::setprecision(2) << throughput_mbps_send << " MB/s" << std::endl;
        std::cout << "Throughput (duplex): " << std::fixed << std::setprecision(2) << throughput_mbps_total << " MB/s ("
                  << std::fixed << std::setprecision(2) << throughput_gbps_total << " Gbps)" << std::endl;
      }
      else
      {
        std::cout << "Error: Time taken was too short to calculate throughput." << std::endl;
      }
    }
    else
    {
      std::cerr << "Invalid number of arguments." << std::endl;
      std::cerr << "Usage for server: " << argv[0] << " <port>" << std::endl;
      std::cerr << "Usage for client: " << argv[0] << " <host> <port> <data_size_mb> <iterations>" << std::endl;
      return 1;
    }
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
