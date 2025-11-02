#include "worker.hpp"
#include "utility/time.hpp"

#include <asio/ip/tcp.hpp>
#include <iostream>

worker::worker(config cfg)
  : config_{std::move(cfg)},
#ifdef IO_URING_API
    io_ctx_{config_.uring_depth, config_.params},
    buffer_pool_{io_ctx_, config_.buffer_size, config_.buffer_count, uring::provided_buffer_pool::group_id_type{0}},
#elifdef ASIO_API
    io_ctx_{1},
#endif
    sample_queue_{1024 * 64}
{
#ifdef IO_URING_API
  buffer_pool_.populate_buffers();
  io_ctx_.init_buffer_pool(config_.buffer_size, 64);
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
    iter->pacing_period_ns = config_.target_msg_rate > 0 ? (1000000000ull / config_.target_msg_rate) : 0ull;
    iter->next_eligible_send_ns = 0;
    
    sock.non_blocking(true);
    sock.set_option(::asio::ip::tcp::no_delay{true});
    
    iter->receiver.open(std::move(sock));
    iter->sender.open(iter->receiver.get_socket());
    iter->receiver.start([this, iter](std::error_code ec, auto&& data) {
      if (ec)
      {
        std::cerr << "Error receiving data: " << ec.message() << std::endl;
        
        if (++closed_conns_ == connections_.size())
        {
          shutdown_counter_->store(0, std::memory_order::relaxed);
        }

        return;
      }

      on_data(*iter, data);
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
    // optional pacing for initiator: wait until next eligible time before sending next message
    if (conn.pacing_period_ns > 0)
    {
      if (conn.next_eligible_send_ns == 0)
      {
        conn.next_eligible_send_ns = now; // first pacing anchor
      }
      else
      {
        // advance schedule by exactly one period; if we're late, skip sleeping
        conn.next_eligible_send_ns += conn.pacing_period_ns;
      }

      auto const now2 = utility::nanos_since_epoch();
      if (conn.next_eligible_send_ns > now2)
      {
        auto const wait_ns = conn.next_eligible_send_ns - now2;
        std::this_thread::sleep_for(std::chrono::nanoseconds{wait_ns});
      }
    }

    conn.send(data, utility::nanos_since_epoch());

    // std::cout << conn.msg_cnt << std::endl;

    if (++conn.msg_cnt > config_.warmup_count && !sample_queue_.push({.send_ts = send_ts, .recv_ts = now}))
    {
      std::cerr << "Failed to produce latency sample due to queue full\n";
      std::terminate();
    }
  }
  else
  {
    conn.send(data, 0);
  }
}

void worker::run(std::atomic<int>& shutdown_counter)
{
  shutdown_counter_ = &shutdown_counter;
  std::cout << "worker thread " << std::this_thread::get_id() << " started with busy spin polling." << std::endl;

  try
  {
#ifdef ASIO_API
    auto work_guard = ::asio::make_work_guard(io_ctx_);
#endif
    while (shutdown_counter.load(std::memory_order::relaxed) > 0)
    {
      io_ctx_.poll();
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error in worker thread: " << e.what() << std::endl;
  }

  std::cout << "worker thread " << std::this_thread::get_id() << " stopping." << std::endl;
}

void worker::connection::send(::asio::const_buffer const data, std::uint64_t const send_ts)
{
  // receiver.get_socket().send(data, 0);
  sender.send(data.data(), data.size());
  this->send_ts = send_ts;
}