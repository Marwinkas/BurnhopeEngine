#pragma once

#include "Core/Types.hpp"
#include <algorithm>
#include <cstdlib>
#include <span>

namespace burnhope {

/** Per-frame bump allocator (16–32 MiB typical). No heap allocs in the game loop. */
class FrameArena final : public NonCopyable {
public:
  static constexpr std::size_t kDefaultCapacity = 24 * 1024 * 1024;
  static constexpr std::size_t kAlignment = 64;

  explicit FrameArena(std::size_t capacity = kDefaultCapacity) {
    base_ = static_cast<std::byte*>(std::aligned_alloc(kAlignment, capacity));
    if (!base_) {
      throwVkError("FrameArena: aligned_alloc failed");
    }
    capacity_ = capacity;
    offset_ = 0;
  }

  ~FrameArena() {
    if (base_) {
      std::free(base_);
    }
  }

  void reset() noexcept { offset_ = 0; }

  [[nodiscard]] std::span<std::byte> allocate(std::size_t size, std::size_t align = kAlignment) {
    const std::size_t aligned = static_cast<std::size_t>(alignUp(offset_, align));
    if (aligned + size > capacity_) {
      throwVkError("FrameArena: out of memory");
    }
    offset_ = aligned + size;
    return {base_ + aligned, size};
  }

  template <class T>
  [[nodiscard]] T* allocateArray(std::size_t count, std::size_t align = alignof(T)) {
    auto bytes = allocate(count * sizeof(T), std::max(align, kAlignment));
    return reinterpret_cast<T*>(bytes.data());
  }

  [[nodiscard]] std::size_t used() const noexcept { return offset_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
  std::byte* base_{nullptr};
  std::size_t capacity_{0};
  std::size_t offset_{0};
};

} // namespace burnhope
