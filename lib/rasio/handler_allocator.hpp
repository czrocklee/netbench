#pragma once

#include <asio/associated_allocator.hpp>

#include <cassert>
#include <memory>
#include <new>
#include <type_traits>
#include <variant>

namespace rasio
{
  template<std::size_t Size>
  class static_handler_memory
  {
  public:
    void* allocate(std::size_t size)
    {
      if (size > Size)
      {
        throw std::bad_alloc{};
      }

      return std::addressof(storage_);
    }

    void deallocate(void* ptr, std::size_t size)
    {
      assert(size <= Size);
      assert(ptr == std::addressof(storage_));
    }

  private:
    alignas(sizeof(std::size_t)) std::byte storage_[Size];
  };

  class dynamic_handler_memory
  {
  public:
    dynamic_handler_memory() = default;

    explicit dynamic_handler_memory(std::size_t size) : storage_ptr_{std::make_unique<char[]>(size)}, size_{size} {}

    void* allocate(std::size_t size)
    {
      if (size > size_)
      {
        storage_ptr_ = std::make_unique<char[]>(size);
        size_ = size;
      }

      return storage_ptr_.get();
    }

    void deallocate(void* ptr, std::size_t size)
    {
      assert(size == size_);
      assert(ptr == storage_ptr_.get());
    }

    void* get() const noexcept { return storage_ptr_.get(); }

    std::size_t size() const noexcept { return size_; }

  private:
    std::unique_ptr<char[]> storage_ptr_;
    std::size_t size_ = 0;
  };

  template<std::size_t Size>
  class small_handler_memory
  {
  public:
    small_handler_memory() = default;

    void* allocate(std::size_t size)
    {
      if (auto* dynamic_ptr = std::get_if<dynamic_handler_memory>(&memory_); dynamic_ptr != nullptr)
      {
        return dynamic_ptr->allocate(size);
      }

      return size <= Size ? std::get<static_handler_memory<Size>>(memory_).allocate(size)
                          : memory_.template emplace<dynamic_handler_memory>(size).allocate(size);
    }

    void deallocate(void* ptr, std::size_t size)
    {
      std::visit([&](auto& m) { m.deallocate(ptr, size); }, memory_);
    }

  private:
    std::variant<static_handler_memory<Size>, dynamic_handler_memory> memory_;
  };

  template<typename T, typename MemoryT>
  class handler_allocator
  {
  public:
    using value_type = T;

    explicit handler_allocator(MemoryT& memory) : memory_{memory} {}

    template<typename U>
    handler_allocator(handler_allocator<U, MemoryT> const& other) noexcept : memory_{other.memory_}
    {
    }

    bool operator==(handler_allocator const& other) const noexcept { return &memory_ == &other.memory_; }

    bool operator!=(handler_allocator const& other) const noexcept { return &memory_ != &other.memory_; }

    T* allocate(std::size_t n) const { return static_cast<T*>(memory_.allocate(sizeof(T) * n)); }

    void deallocate(T* p, std::size_t n) const { return memory_.deallocate(p, sizeof(T) * n); }

  private:
    template<typename, typename>
    friend class handler_allocator;
    MemoryT& memory_;
  };

  template<typename Handler, typename MemoryT>
  class custom_alloc_handler
  {
  public:
    using allocator_type = handler_allocator<Handler, MemoryT>;

    template<typename HandlerArg>
    custom_alloc_handler(MemoryT& memory, HandlerArg&& handler)
      : memory_{memory}, handler_{std::forward<HandlerArg>(handler)}
    {
    }

    allocator_type get_allocator() const noexcept { return allocator_type(memory_); }

    template<typename... Args>
    decltype(auto) operator()(Args&&... args)
    {
      return handler_(std::forward<Args>(args)...);
    }

  private:
    MemoryT& memory_;
    Handler handler_;
  };

  // Helper function to wrap a handler object to add custom allocation.
  template<typename Handler, typename MemoryT>
  inline custom_alloc_handler<Handler, MemoryT> make_custom_alloc_handler(MemoryT& memory, Handler&& handler)
  {
    return {memory, std::forward<Handler>(handler)};
  }

  template<typename Handler, typename Action>
  struct forward_alloc_handler
  {
  public:
    using allocator_type = typename ::asio::associated_allocator<Handler>::type;

    template<typename HandlerArg, typename ActionArg>
    forward_alloc_handler(HandlerArg&& handler, ActionArg&& action)
      : handler_{std::forward<HandlerArg>(handler)}, action_{std::forward<ActionArg>(action)}
    {
    }

    allocator_type get_allocator() const noexcept { return ::asio::associated_allocator<Handler>::get(handler_); }

    template<typename... Args>
    decltype(auto) operator()(Args&&... args)
    {
      return action_(handler_, std::forward<Args>(args)...);
    }

  private:
    Handler handler_;
    Action action_;
  };

  template<typename Handler, typename Action>
  inline decltype(auto) make_forward_alloc_handler(Handler&& handler, Action&& action)
  {
    return forward_alloc_handler<std::decay_t<Handler>, std::decay_t<Action>>{
      std::forward<Handler>(handler), std::forward<Action>(action)};
  }
} // namespace rasio