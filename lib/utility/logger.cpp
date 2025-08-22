#include "logger.hpp"
#include <magic_enum/magic_enum.hpp>
#include <format>
#include <stdexcept>

utility::log_level utility::from_string(std::string_view str)
{
  if (auto lvl = magic_enum::enum_cast<log_level>(str); lvl.has_value())
  {
    return lvl.value();
  }

  throw std::invalid_argument(std::format("invalid log level {}", str));
}