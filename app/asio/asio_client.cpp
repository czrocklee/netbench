// =====================================================================
// Boost.Asio Simple Echo Client
// =====================================================================
// This client connects to a TCP echo server, sends a single message,
// waits for the echoed response, prints it, and then exits.
//
// To compile (requires Boost library or standalone Asio):
// g++ -std=c++17 -o simple_echo_client simple_echo_client.cpp -DBOOST_ASIO_STANDALONE -lboost_system -pthread
//
// To run:
// ./simple_echo_client <server_ip> <port> <message>
// Example: ./simple_echo_client 127.0.0.1 8080 "Hello Asio!"
// =====================================================================

#include <iostream>
#include <string>
#include <vector>
#include <thread> // Required for io_context::run() in a separate thread

// Use standalone Boost.Asio if defined, otherwise use full Boost
#include <asio.hpp>
#include <asio/buffer.hpp>
#include <asio/ip/tcp.hpp>

namespace client
{

  using asio::ip::tcp;
  using tcp = asio::ip::tcp;

  class simple_echo_client
  {
  public:
    // Constructor initializes the client with io_context, endpoints, and the message to send.
    simple_echo_client(asio::io_context &io_context,
                       const tcp::resolver::results_type &endpoints,
                       const std::string &message)
        : io_context_(io_context),
          socket_(io_context),
          message_to_send_(message),
          bytes_to_read_(message.length()) // Expect to read back the same number of bytes
    {
      // Start the asynchronous connection process.
      do_connect(endpoints);
    }

  private:
    // Handles the asynchronous connection attempt.
    void do_connect(const tcp::resolver::results_type &endpoints)
    {
      asio::async_connect(socket_, endpoints,
                          [this](const asio::error_code &error, const tcp::endpoint & /*endpoint*/)
                          {
                            if (!error)
                            {
                              std::cout << "Connected to server: " << socket_.remote_endpoint() << std::endl;
                              do_write(); // If connection is successful, start writing the message.
                            }
                            else
                            {
                              std::cerr << "Connect error: " << error.message() << std::endl;
                              io_context_.stop(); // Stop io_context if connection fails.
                            }
                          });
    }

    // Handles the asynchronous write operation.
    void do_write()
    {
      // Use asio::async_write to ensure all bytes are sent.
      asio::async_write(socket_, asio::buffer(message_to_send_),
                        [this](const asio::error_code &error, size_t bytes_transferred)
                        {
                          if (!error)
                          {
                            std::cout << "Sent " << bytes_transferred << " bytes: '" << message_to_send_ << "'" << std::endl;
                            // Prepare buffer for receiving the echoed message.
                            receive_buffer_.resize(bytes_to_read_);
                            do_read(); // After sending, expect to receive the echo.
                          }
                          else
                          {
                            std::cerr << "Write error: " << error.message() << std::endl;
                            io_context_.stop(); // Stop io_context on write error.
                          }
                        });
    }

    // Handles the asynchronous read operation for the echoed message.
    void do_read()
    {
      // Use asio::async_read to ensure all expected bytes are received.
      asio::async_read(socket_, asio::buffer(receive_buffer_),
                       [this](const asio::error_code &error, size_t bytes_transferred)
                       {
                         if (!error)
                         {
                           std::string received_message(receive_buffer_.data(), bytes_transferred);
                           std::cout << "Received " << bytes_transferred << " bytes: '" << received_message << "'" << std::endl;
                           // If the received message matches the sent message, it's a successful echo.
                           if (received_message == message_to_send_)
                           {
                             std::cout << "Echo successful!" << std::endl;
                           }
                           else
                           {
                             std::cout << "Echo mismatch: Received data does not match sent data." << std::endl;
                           }
                           // Gracefully close the connection after receiving the echo.
                           socket_.shutdown(tcp::socket::shutdown_both);
                           socket_.close();
                           io_context_.stop(); // All work is done, stop the io_context.
                         }
                         else if (error == asio::error::eof || error == asio::error::connection_reset)
                         {
                           std::cerr << "Server disconnected prematurely or connection reset." << std::endl;
                           io_context_.stop();
                         }
                         else
                         {
                           std::cerr << "Read error: " << error.message() << std::endl;
                           io_context_.stop(); // Stop io_context on read error.
                         }
                       });
    }

    asio::io_context &io_context_;
    tcp::socket socket_;
    std::string message_to_send_;
    std::vector<char> receive_buffer_; // Use a vector for dynamic buffer size
    size_t bytes_to_read_;
  };

} // namespace client

// Main entry point for the client application.
int main(int argc, char *argv[])
{
  try
  {
    // Check for correct command-line arguments.
    if (argc != 4)
    {
      std::cerr << "Usage: " << argv[0] << " <host> <port> <message>" << std::endl;
      std::cerr << "Example: " << argv[0] << " 127.0.0.1 8080 \"Hello Boost.Asio!\"" << std::endl;
      return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];
    std::string message = argv[3];

    asio::io_context io_context; // The core of Asio's asynchronous operations.

    // Resolve the server's address and port.
    asio::ip::tcp::resolver resolver(io_context);
    asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, port);

    // Create an instance of our simple echo client.
    client::simple_echo_client c(io_context, endpoints, message);

    // Run the io_context. This will block until all asynchronous operations
    // are complete or io_context::stop() is called.
    // Running it in a separate thread can be useful for more complex apps
    // but for a simple send/receive, direct run() is fine too.
    io_context.run();

    std::cout << "Client finished." << std::endl;
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
