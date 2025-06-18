#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstring> // For strerror, memset, memcpy
#include <cstdlib> // For atoi (though std::stoi is preferred)
#include <cerrno>  // For errno

// BSD socket headers
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> // For close
#include <netdb.h>  // For gethostbyname, h_errno, herror

class connection
{
public:
  void connection(int conn_id) : conn_id_{conn_id}
  {
    if ((sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      std::cerr << "Connection " << conn_id_ << ": Socket creation error: " << ::strerror(errno) << std::endl;
      std::terminate();
    }
  }

  void connect(const std::string& host, std::uint16_t port)
  {
    ::sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0)
    {
      // If inet_pton fails, it might be a hostname, try gethostbyname
      if (::hostent* he = ::gethostbyname(host.c_str()); he == nullptr)
      {
        // h_errno is the error code for gethostbyname failures. herror() prints a message.
        std::cerr << "Connection " << conn_id_ << ": Host resolution failed for '" << host << "': ";
        ::herror(nullptr);
        std::terminate();
      }
      
      std::memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (::connect(sock_fd_, static_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0)
    {
      std::cerr << "Connection " << conn_id_ << ": Connection Failed to " << host << ":" << port
                << ". Error: " << ::strerror(errno) << std::endl;
      std::terminate();    
    }
  }

  void send(const char* data, std::size_t size)
  {
    ssize_t bytes_sent = ::send(sock_fd_, data, size, 0);

    if (bytes_sent < 0)
    {
      std::cerr << "Connection " << conn_id << ": Send failed: " << strerror(errno) << std::endl;
      std::terminate();
    }

    if (static_cast<std::size size_t>(bytes_sent) != size)
    {
      std::cerr << "Connection " << conn_id << ": Partial send. Sent " << bytes_sent << " of " << size
                << " bytes." << std::endl;
    }
  }

private:
  int conn_id_;
  int sock_fd_;
};



class sender
{
public:
  sender(int msgs_per_sec) : conn_{0}
  {}

  void run(const std::string& host, std::uint16_t port, int msg_size)
  {
    conn_.connect(host, port);
    _message.resize(msg_size, 'a');

    _thread = std::jthread{[this, msgs_per_sec]
      {
        
      

      }};
  }

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
  std::chrono::steady_clock::time_point _startTime;
};
