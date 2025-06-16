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

class sender
{
public:
  void connect();

private:
  int fd;
};

// Function to handle sending messages for a single connection
void sender_worker(const std::string& host, int port, int msgs_per_sec, int msg_size, int conn_id)
{
  int sock_fd = -1;
  struct sockaddr_in serv_addr;
  std::vector<char> message_buffer(msg_size);
  // Fill message_buffer with some data, e.g., 'A'
  std::memset(message_buffer.data(), 'A', msg_size);

  // Creating socket file descriptor
  if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    std::cerr << "Connection " << conn_id << ": Socket creation error: " << strerror(errno) << std::endl;
    return;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  // Convert IPv4 addresses from text to binary form
  // Try inet_pton first for numeric IP addresses
  if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0)
  {
    // If inet_pton fails, it might be a hostname, try gethostbyname
    struct hostent* he = gethostbyname(host.c_str());
    if (he == nullptr)
    {
      // h_errno is the error code for gethostbyname failures. herror() prints a message.
      std::cerr << "Connection " << conn_id << ": Host resolution failed for '" << host << "': ";
      herror(nullptr); // Prints error message based on h_errno
      close(sock_fd);
      return;
    }
    // Assuming IPv4. he->h_addr_list[0] contains the first address.
    memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
  }

  // Connecting to the server
  if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
  {
    std::cerr << "Connection " << conn_id << ": Connection Failed to " << host << ":" << port
              << ". Error: " << strerror(errno) << std::endl;
    close(sock_fd);
    return;
  }
  std::cout << "Connection " << conn_id << ": Connected to " << host << ":" << port << std::endl;

  long long sleep_duration_us = 0;
  if (msgs_per_sec > 0)
  {
    sleep_duration_us = static_cast<long long>(1000000.0 / msgs_per_sec);
    if (sleep_duration_us < 0) sleep_duration_us = 0; // Should not happen with positive msgs_per_sec
  }
  else if (msgs_per_sec == 0)
  { // Send as fast as possible
    sleep_duration_us = 0;
  }
  else
  {
    std::cerr << "Connection " << conn_id << ": Invalid msgs_per_sec value: " << msgs_per_sec << ". Must be >= 0."
              << std::endl;
    close(sock_fd);
    return;
  }

  if (msgs_per_sec > 0)
  {
    std::cout << "Connection " << conn_id << ": Sending " << msgs_per_sec << " msgs/sec, msg_size " << msg_size
              << " bytes. Approx sleep per msg: " << sleep_duration_us << " us." << std::endl;
  }
  else
  {
    std::cout << "Connection " << conn_id << ": Sending at max speed, msg_size " << msg_size << " bytes." << std::endl;
  }

  while (true)
  {
    ssize_t bytes_sent = send(sock_fd, message_buffer.data(), message_buffer.size(), 0);
    if (bytes_sent < 0)
    {
      std::cerr << "Connection " << conn_id << ": Send failed: " << strerror(errno) << std::endl;
      break;
    }
    if (static_cast<size_t>(bytes_sent) != message_buffer.size())
    {
      std::cerr << "Connection " << conn_id << ": Partial send. Sent " << bytes_sent << " of " << message_buffer.size()
                << " bytes." << std::endl;
      // For a simple load tester, this might be an error condition to stop or log.
    }

    if (msgs_per_sec > 0 && sleep_duration_us > 0)
    {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_duration_us));
    }
  }

  close(sock_fd);
  std::cout << "Connection " << conn_id << ": Closed." << std::endl;
}