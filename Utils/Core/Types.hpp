#pragma once

#include <cstring>
#include <cstdint>
#include <span>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace burnhope {

[[nodiscard]] constexpr bool isPow2(uint64_t v) noexcept {
  return v != 0 && (v & (v - 1)) == 0;
}

[[nodiscard]] constexpr uint64_t alignUp(uint64_t v, uint64_t alignment) noexcept {
  return (v + alignment - 1) & ~(alignment - 1);
}

template <class T>
  requires std::is_trivially_copyable_v<T>
void copyBytes(std::span<const std::byte> src, std::span<std::byte> dst) noexcept {
  if (src.size() > dst.size()) {
    return;
  }
  std::memcpy(dst.data(), src.data(), src.size());
}

template <class T>
  requires std::is_trivially_copyable_v<T>
void writePod(std::span<std::byte> dst, const T& value) noexcept {
  copyBytes<T>(std::as_bytes(std::span{&value, 1}), dst);
}

[[noreturn]] inline void throwVkError(
    std::string_view what,
    std::source_location loc = std::source_location::current()) {
  throw std::runtime_error(
      std::string{what} + " [" + loc.file_name() + ":" + std::to_string(loc.line()) + "]");
}

struct NonCopyable {
  NonCopyable() = default;
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
};

} // namespace burnhope
