#pragma once

#include <variant>
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace utility
{

  // ref_or_own: stores either a reference to T or owns a T
  // Usage: ref_or_own<T> x{ref}; or ref_or_own<T> x{value};
  template<typename T>
  class ref_or_own

  {
  public:
    // Default constructor: creates a blank state
    ref_or_own() : storage_{std::monostate{}} {}

    // Construct from reference
    ref_or_own(T& ref) : storage_{ref_type{ref}} {}
    // Construct from value
    ref_or_own(T&& value) : storage_{std::move(value)} {}
    ref_or_own(T const& value) : storage_{own_type{value}} {}
    // In-place constructor: constructs T directly from arguments
    template<typename... Args>
    explicit ref_or_own(std::in_place_t, Args&&... args) : storage_{own_type{std::forward<Args>(args)...}}
    {
    }

    // Access as reference
    T& get()
    {
      if (std::holds_alternative<ref_type>(storage_))
      {
        return std::get<ref_type>(storage_).get();
      }
      else if (std::holds_alternative<own_type>(storage_))
      {
        return std::get<own_type>(storage_);
      }

      throw std::runtime_error{"ref_or_own: get() called on blank value"};
    }

    T const& get() const
    {
      if (std::holds_alternative<ref_type>(storage_))
      {
        return std::get<ref_type>(storage_).get();
      }
      else if (std::holds_alternative<own_type>(storage_))
      {
        return std::get<own_type>(storage_);
      }

      throw std::runtime_error{"ref_or_own: get() called on blank value"};
    }

    bool is_ref() const { return std::holds_alternative<ref_type>(storage_); }

    bool is_own() const { return std::holds_alternative<own_type>(storage_); }

  private:
    using ref_type = std::reference_wrapper<T>;
    using own_type = T;
    std::variant<std::monostate, ref_type, own_type> storage_;
  };

} // namespace utility
