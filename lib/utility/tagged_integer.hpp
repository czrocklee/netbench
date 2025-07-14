#pragma once

#include <cstddef>
#include <limits>

namespace utility
{
  template<typename T, typename Tag, T Default = T{}>
  class tagged_integer
  {
  public:
    using value_type = T;

    constexpr tagged_integer() noexcept : value_{Default} {}
    constexpr explicit tagged_integer(T value) noexcept : value_(value) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T value() const noexcept { return value_; }
    static constexpr tagged_integer invalid() noexcept { return {}; }
    constexpr bool is_valid() const noexcept { return value_ != Default; }
    template<typename U>
    static tagged_integer cast_from(U u) { return tagged_integer{static_cast<T>(u)}; }

    // clang-format off
    constexpr tagged_integer& operator++() noexcept { ++value_; return *this; }
    constexpr tagged_integer operator++(int) noexcept { tagged_integer tmp = *this; ++value_; return tmp; }
    constexpr tagged_integer& operator--() noexcept { --value_; return *this; }
    constexpr tagged_integer operator--(int) noexcept { tagged_integer tmp = *this; --value_; return tmp; }
    constexpr tagged_integer& operator+=(T v) noexcept { value_ += v; return *this; }
    constexpr tagged_integer& operator-=(T v) noexcept { value_ -= v; return *this; }
    // clang-format on

    friend constexpr tagged_integer operator+(tagged_integer a, T b) noexcept { return tagged_integer(a.value_ + b); }
    friend constexpr tagged_integer operator-(tagged_integer a, T b) noexcept { return tagged_integer(a.value_ - b); }

    friend constexpr bool operator<(tagged_integer a, tagged_integer b) noexcept { return a.value_ < b.value_; }
    friend constexpr bool operator==(tagged_integer a, tagged_integer b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(tagged_integer a, tagged_integer b) noexcept { return a.value_ != b.value_; }

  private:
    T value_;
  };

  template<typename Tag>
  using tagged_index = tagged_integer<std::size_t, Tag, std::numeric_limits<std::size_t>::max()>;
}
