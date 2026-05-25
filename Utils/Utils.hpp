#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace burnhope {

/** MurmurHash3 64-bit — runtime entity/asset IDs (no string paths in hot path). */
[[nodiscard]] inline uint64_t murmurHash3_64(std::span<const std::byte> data, uint64_t seed = 0) noexcept {
  constexpr uint64_t c1 = 0x87c37b91114253d5ull;
  constexpr uint64_t c2 = 0x4cf5ad432745937full;
  const auto rotl = [](uint64_t x, int r) { return (x << r) | (x >> (64 - r)); };

  uint64_t h1 = seed;
  const std::size_t nblocks = data.size() / 16;
  const auto* blocks = reinterpret_cast<const uint64_t*>(data.data());

  for (std::size_t i = 0; i < nblocks; ++i) {
    uint64_t k1 = blocks[i * 2 + 0];
    uint64_t k2 = blocks[i * 2 + 1];
    k1 *= c1;
    k1 = rotl(k1, 31);
    k1 *= c2;
    h1 ^= k1;
    h1 = rotl(h1, 27);
    h1 = h1 * 5 + 0x52dce729;
    k2 *= c2;
    k2 = rotl(k2, 33);
    k2 *= c1;
    h1 ^= k2;
    h1 = rotl(h1, 31);
    h1 = h1 * 5 + 0x38495ab5;
  }

  const std::size_t tail = nblocks * 16;
  const auto* tailBytes = reinterpret_cast<const uint8_t*>(data.data() + tail);
  uint64_t k1 = 0;
  uint64_t k2 = 0;
  switch (data.size() - tail) {
  case 15:
    k2 ^= static_cast<uint64_t>(tailBytes[14]) << 48;
    [[fallthrough]];
  case 14:
    k2 ^= static_cast<uint64_t>(tailBytes[13]) << 40;
    [[fallthrough]];
  case 13:
    k2 ^= static_cast<uint64_t>(tailBytes[12]) << 32;
    [[fallthrough]];
  case 12:
    k2 ^= static_cast<uint64_t>(tailBytes[11]) << 24;
    [[fallthrough]];
  case 11:
    k2 ^= static_cast<uint64_t>(tailBytes[10]) << 16;
    [[fallthrough]];
  case 10:
    k2 ^= static_cast<uint64_t>(tailBytes[9]) << 8;
    [[fallthrough]];
  case 9:
    k2 ^= static_cast<uint64_t>(tailBytes[8]);
    k2 *= c2;
    k2 = rotl(k2, 33);
    k2 *= c1;
    h1 ^= k2;
    [[fallthrough]];
  case 8:
    k1 ^= static_cast<uint64_t>(tailBytes[7]) << 56;
    [[fallthrough]];
  case 7:
    k1 ^= static_cast<uint64_t>(tailBytes[6]) << 48;
    [[fallthrough]];
  case 6:
    k1 ^= static_cast<uint64_t>(tailBytes[5]) << 40;
    [[fallthrough]];
  case 5:
    k1 ^= static_cast<uint64_t>(tailBytes[4]) << 32;
    [[fallthrough]];
  case 4:
    k1 ^= static_cast<uint64_t>(tailBytes[3]) << 24;
    [[fallthrough]];
  case 3:
    k1 ^= static_cast<uint64_t>(tailBytes[2]) << 16;
    [[fallthrough]];
  case 2:
    k1 ^= static_cast<uint64_t>(tailBytes[1]) << 8;
    [[fallthrough]];
  case 1:
    k1 ^= static_cast<uint64_t>(tailBytes[0]);
    k1 *= c1;
    k1 = rotl(k1, 31);
    k1 *= c2;
    h1 ^= k1;
  default:
    break;
  }

  h1 ^= static_cast<uint64_t>(data.size());
  h1 ^= h1 >> 33;
  h1 *= 0xff51afd7ed558ccdull;
  h1 ^= h1 >> 33;
  h1 *= 0xc4ceb9fe1a85ec53ull;
  h1 ^= h1 >> 33;
  return h1;
}

[[nodiscard]] inline uint64_t hashString64(std::string_view s) noexcept {
  return murmurHash3_64(std::span{
      reinterpret_cast<const std::byte*>(s.data()),
      s.size()});
}

} // namespace burnhope
