#include "worker.hpp"
#include "utility/time.hpp"

#include <asio/ip/tcp.hpp>
#include <iostream>

worker::worker(config cfg)
  : config_{std::move(cfg)},
#ifdef IO_URING_API
    io_ctx_{config_.uring_depth, config_.params},
    buffer_pool_{io_ctx_, config_.buffer_count, config_.buffer_size, uring::provided_buffer_pool::group_id_type{0}},
    fixed_buffer_pool_{io_ctx_, 1024, 4096},
#elifdef ASIO_API
    io_ctx_{1},
#endif
    sample_queue_{1024 * 64}
{
#ifdef IO_URING_API
  buffer_pool_.populate_buffers();
#endif
}

void worker::send_initial_message()
{
  unsigned char c = 'a';

  for (auto& conn : connections_)
  {
    std::vector<std::byte> msg;
    msg.resize(conn.msg_size, std::byte{c});
    conn.send(::asio::buffer(msg), utility::nanos_since_epoch());
    c = (c + 1) % 'a';
  }
}

void worker::add_connection(net::socket sock, std::size_t msg_size)
{
  try
  {
#ifdef IO_URING_API
    auto iter = connections_.emplace(connections_.begin(), io_ctx_, buffer_pool_);
#else
    auto iter = connections_.emplace(connections_.begin(), io_ctx_, config_.buffer_size);
#endif
    iter->msg_size = msg_size;
    sock.set_option(::asio::ip::tcp::no_delay{true});
    iter->receiver.open(std::move(sock));
#ifdef IO_URING_API
    iter->sender.open(iter->receiver.get_socket());
    iter->sender.enable_fixed_buffer_fastpath(fixed_buffer_pool_);
#endif
    iter->receiver.start([this, iter](std::error_code ec, auto&& data) {
      if (ec)
      {
        std::cerr << "Error receiving data: " << ec.message() << std::endl;
        connections_.erase(iter);
        return;
      }

#ifdef IO_URING_API
      for (auto& buf : data) { on_data(*iter, buf); }
#else
      on_data(*iter, data);
#endif
    });
  }
  catch (std::exception const& e)
  {
    std::cerr << "Failed to create connection from accepted socket: " << e.what() << std::endl;
  }
}

void worker::on_data(connection& conn, ::asio::const_buffer const data)
{
  if (data.size() != conn.msg_size)
  {
    std::cerr << "Partial read in pingpong test, not tolerable \n";
    std::terminate();
  }

  if (conn.send_ts > 0)
  {
    auto now = utility::nanos_since_epoch();
    auto send_ts = conn.send_ts;
    conn.send(data, now);

    constexpr auto warm_up_msg_cnt = 10000;
    // std::cout << conn.msg_cnt << std::endl;

    if (++conn.msg_cnt > warm_up_msg_cnt && !sample_queue_.push({.send_ts = send_ts, .recv_ts = now}))
    {
      std::cerr << "Failed to produce latency sample due to queue full\n";
      std::terminate();
    }
  }
  else { conn.send(data, 0); }
}

void worker::run(std::atomic<bool>& stop_flag)
{
  std::cout << "worker thread " << std::this_thread::get_id() << " started with busy spin polling." << std::endl;

  try
  {
#ifdef ASIO_API
    auto work_guard_ = ::asio::make_work_guard(io_ctx_);
    while (!stop_flag.load(std::memory_order::relaxed)) { io_ctx_.poll(); }

#else
    while (!stop_flag.load(std::memory_order::relaxed)) { io_ctx_.poll(); }
#endif
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error in worker thread: " << e.what() << std::endl;
  }

  std::cout << "worker thread " << std::this_thread::get_id() << " stopping." << std::endl;
}

void worker::connection::send(::asio::const_buffer const data, std::uint64_t const send_ts)
{
#ifdef IO_URING_API
  sender.send([data](void* buf, std::size_t size) {
    if (size < data.size())
    {
      std::cerr << "Buffer size is smaller than data size, not tolerable in pingpong test\n";
      std::terminate();
    }

    std::memcpy(buf, data.data(), data.size());
    return data.size();
  });
#else
  auto bytes = receiver.get_socket().send(data, 0);

  if (bytes != data.size())
  {
    std::cerr << "Partial write in pingpong test, not tolerable \n";
    std::terminate();
  }
#endif

  this->send_ts = send_ts;
}