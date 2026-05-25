#pragma once

#include <bit>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace burnhope {

/** Pack VkDeviceAddress for push/UBO without reinterpret_cast (strict aliasing safe). */
[[nodiscard]] inline uint64_t deviceAddressBits(VkDeviceAddress address) noexcept {
  return std::bit_cast<uint64_t>(address);
}

[[nodiscard]] inline VkDeviceAddress deviceAddressFromBits(uint64_t bits) noexcept {
  return std::bit_cast<VkDeviceAddress>(bits);
}

} // namespace burnhope
