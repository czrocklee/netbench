#pragma once

#include <string>

namespace utility
{
  enum class log_level
  {
    trace = 0,
    debug,
    info,
    warn,
    error,
    critical,
    off
  };

  log_level from_string(std::string_view str);
}

#ifdef USE_SPDLOG_LOGGER

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>

namespace utility
{
  inline void init_log_file(std::string const& filename)
  {
    auto file_logger = spdlog::basic_logger_mt("file_logger", filename);
    spdlog::set_default_logger(file_logger);
  }

  inline void set_log_level(log_level lvl)
  {
    using enum log_level;

    switch (lvl)
    {
      case trace: spdlog::set_level(spdlog::level::trace); break;
      case debug: spdlog::set_level(spdlog::level::debug); break;
      case info: spdlog::set_level(spdlog::level::info); break;
      case warn: spdlog::set_level(spdlog::level::warn); break;
      case error: spdlog::set_level(spdlog::level::err); break;
      case critical: spdlog::set_level(spdlog::level::critical); break;
      case off: spdlog::set_level(spdlog::level::off); break;
    }
  }
}

#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

#elif defined(USE_STDERR_LOGGER)

#include <iostream>
#include <format>
#include <atomic>

namespace utility
{
  inline std::atomic<log_level> min_log_level = log_level::info;

  inline void set_log_level(log_level lvl)
  {
    min_log_level.store(lvl, std::memory_order_relaxed);
  }

  inline void init_log_file(std::string const&)
  {
  }
}

template<typename... Args>
inline void log_stderr(std::format_string<Args...> fmt, Args&&... args)
{
  std::println(std::cerr, fmt, std::forward<Args>(args)...);
}

// Macro to reduce duplication for log level checks
#define LOG_LEVEL_MACRO(LOGLEVEL, PREFIX, ...)                                                                         \
  do {                                                                                                                 \
    if ((LOGLEVEL) >= utility::min_log_level.load(std::memory_order_relaxed)) { log_stderr(PREFIX __VA_ARGS__); }      \
  } while (0)

#define LOG_TRACE(...) LOG_LEVEL_MACRO(::utility::log_level::trace, "[TRACE] ", __VA_ARGS__)
#define LOG_DEBUG(...) LOG_LEVEL_MACRO(::utility::log_level::debug, "[DEBUG] ", __VA_ARGS__)
#define LOG_INFO(...) LOG_LEVEL_MACRO(::utility::log_level::info, "[INFO ] ", __VA_ARGS__)
#define LOG_WARN(...) LOG_LEVEL_MACRO(::utility::log_level::warn, "[WARN ] ", __VA_ARGS__)
#define LOG_ERROR(...) LOG_LEVEL_MACRO(::utility::log_level::error, "[ERROR] ", __VA_ARGS__)
#define LOG_CRITICAL(...) LOG_LEVEL_MACRO(::utility::log_level::critical, "[CRIT ] ", __VA_ARGS__)

#elif !defined(USE_CUSTOM_LOGGER)

namespace utility
{
  inline void init_log_file(std::string const&)
  {
  }

  inline void set_log_level(log_level)
  {
  }
}

#define LOG_TRACE(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)

#endif