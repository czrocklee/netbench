#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstring> // For strerror, memset, memcpy
#include <cstdlib> // For atoi (though std::stoi is preferred)
#include <cerrno>  // For errno

// BSD socket headers
#include <sys/socket.h>
#include <netinet/in.h> // For sockaddr_in
#include <unistd.h> // For close
#include <netdb.h>  // For gethostbyname, h_errno, herror

#include <thread>

class connection
{
public:
  connection(int conn_id) : conn_id_{conn_id}
  {
    if ((sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      std::cerr << "Connection " << conn_id_ << ": Socket creation error: " << ::strerror(errno) << std::endl;
      std::terminate();
    }
  }

  void connect(const std::string& host, const std::string& port)
  {

    ::addrinfo hints;
    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // Stream socket
    hints.ai_flags = 0;              // No flags
    hints.ai_protocol = 0;           // Any protocol

    ::addrinfo* result;

    if (int s = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &result); s != 0)
    {
      std::cerr << "Connection " << conn_id_ << ": getaddrinfo: " << ::gai_strerror(s) << std::endl;
      std::terminate();
    }

    ::addrinfo* rp;

    for (rp = result; rp != nullptr; rp = rp->ai_next)
    {
      if (::connect(sock_fd_, rp->ai_addr, rp->ai_addrlen) == 0)
      {
        // Success
        break;
      }
      else
      {
        std::cerr << "Connection " << conn_id_ << ": Connect failed for address: " << ::strerror(errno) << std::endl;
      }
    }

    ::freeaddrinfo(result); // Free the memory allocated by getaddrinfo

    if (rp == nullptr)
    {
      std::cerr << "Connection " << conn_id_ << ": Could not connect to any address." << std::endl;
      std::terminate();
    }
  }

  void send(const char* data, std::size_t size)
  {
    ssize_t bytes_sent = ::send(sock_fd_, data, size, 0);

    if (bytes_sent < 0)
    {
      std::cerr << "Connection " << conn_id_ << ": Send failed: " << strerror(errno) << std::endl;
      std::terminate();
    }

    if (static_cast<std::size_t>(bytes_sent) != size)
    {
      std::cerr << "Connection " << conn_id_ << ": Partial send. Sent " << bytes_sent << " of " << size << " bytes."
                << std::endl;
    }
  }

private:
  int conn_id_;
  int sock_fd_;
};

class sender
{
public:
  sender(int msgs_per_sec)
    : conn_{0}, interval_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) / msgs_per_sec}
  {
  }

  void run(const std::string& host, const std::string& port, int msg_size)
  {
    conn_.connect(host, port);
    message_.resize(msg_size, std::byte{'a'});
    const auto now = std::chrono::steady_clock::now();
    start_time_ = now;

    _thread = std::jthread{[this] {

      while (true)
      {
        const auto now = std::chrono::steady_clock::now();
        const auto expected_msgs = static_cast<std::uint64_t>((now - start_time_) / interval_);
        //std::cout << (now - start_time_).count() << " " << interval_.count() << std::endl;


        if (msgs_sent < expected_msgs)
        {
          conn_.send(reinterpret_cast<const char*>(message_.data()), message_.size());
          total_msgs_sent_.store(total_msgs_sent_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
          ++msgs_sent;
        }
      }
    }};
  }

  std::uint64_t total_msgs_sent() const { return total_msgs_sent_.load(std::memory_order_relaxed); }

private:
  /*
  struct conn_data
  {
    connection conn_;
    std::chrono::steady_clock::timepoint begin_time;
    std::uint64_t msgs_sent = 0;
  };*/

  connection conn_;
  std::vector<std::byte> message_;
  std::jthread _thread;
  std::chrono::steady_clock::time_point start_time_;
  const std::chrono::steady_clock::duration interval_;
  std::uint64_t msgs_sent = 0;
  std::atomic<std::uint64_t> total_msgs_sent_ = 0;
};
