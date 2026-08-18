#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

// Minimal MurmurHash3 x64_128 (public domain, Austin Appleby) reduced to a
// single 64-bit output. Used engine-wide for asset/entity GUIDs so nothing
// at runtime has to hash-on-demand with a slower generic std::hash.
namespace burnhope::hash {

    inline uint64_t rotl64(uint64_t x, int8_t r) noexcept {
        return (x << r) | (x >> (64 - r));
    }

    inline uint64_t fmix64(uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    // MurmurHash3_x64_128, folded down to 64 bits (h1 ^ h2).
    inline uint64_t Murmur3_64(const void* key, size_t len, uint64_t seed = 0xB00B1E5ULL) noexcept {
        const uint8_t* data = static_cast<const uint8_t*>(key);
        const size_t nblocks = len / 16;

        uint64_t h1 = seed;
        uint64_t h2 = seed;

        constexpr uint64_t c1 = 0x87c37b91114253d5ULL;
        constexpr uint64_t c2 = 0x4cf5ad432745937fULL;

        const uint64_t* blocks = reinterpret_cast<const uint64_t*>(data);
        for (size_t i = 0; i < nblocks; i++) {
            uint64_t k1 = blocks[i * 2 + 0];
            uint64_t k2 = blocks[i * 2 + 1];

            k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
            h1 = rotl64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;

            k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
            h2 = rotl64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
        }

        const uint8_t* tail = data + nblocks * 16;
        uint64_t k1 = 0, k2 = 0;
        switch (len & 15) {
            case 15: k2 ^= uint64_t(tail[14]) << 48; [[fallthrough]];
            case 14: k2 ^= uint64_t(tail[13]) << 40; [[fallthrough]];
            case 13: k2 ^= uint64_t(tail[12]) << 32; [[fallthrough]];
            case 12: k2 ^= uint64_t(tail[11]) << 24; [[fallthrough]];
            case 11: k2 ^= uint64_t(tail[10]) << 16; [[fallthrough]];
            case 10: k2 ^= uint64_t(tail[9]) << 8;   [[fallthrough]];
            case 9:  k2 ^= uint64_t(tail[8]) << 0;
                     k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
                     [[fallthrough]];
            case 8:  k1 ^= uint64_t(tail[7]) << 56; [[fallthrough]];
            case 7:  k1 ^= uint64_t(tail[6]) << 48; [[fallthrough]];
            case 6:  k1 ^= uint64_t(tail[5]) << 40; [[fallthrough]];
            case 5:  k1 ^= uint64_t(tail[4]) << 32; [[fallthrough]];
            case 4:  k1 ^= uint64_t(tail[3]) << 24; [[fallthrough]];
            case 3:  k1 ^= uint64_t(tail[2]) << 16; [[fallthrough]];
            case 2:  k1 ^= uint64_t(tail[1]) << 8;  [[fallthrough]];
            case 1:  k1 ^= uint64_t(tail[0]) << 0;
                     k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
        }

        h1 ^= len; h2 ^= len;
        h1 += h2; h2 += h1;
        h1 = fmix64(h1); h2 = fmix64(h2);
        h1 += h2; h2 += h1;

        return h1 ^ h2;
    }

    inline uint64_t HashString(std::string_view s, uint64_t seed = 0xB00B1E5ULL) noexcept {
        return Murmur3_64(s.data(), s.size(), seed);
    }

    // consteval-friendly hash for compile-time type/tag names (Cooker hashes,
    // pack tables). Not used for hot-path string ops, only fixed identifiers.
    consteval uint64_t HashStringLiteral(const char* s, size_t len, uint64_t seed = 0xB00B1E5ULL) {
        // Simplified constexpr FNV-1a fold; used only for compile-time constants
        // (component type tags), not as a drop-in for the runtime Murmur3 hash.
        uint64_t h = 1469598103934665603ULL ^ seed;
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<uint64_t>(s[i]);
            h *= 1099511628211ULL;
        }
        return h;
    }

    // Runtime-unique 64-bit GUID generator: mixes a monotonic counter with
    // high-resolution time entropy through MurmurHash3 avalanche (fmix64).
    // Not a content hash — purpose is collision-free entity/asset identity
    // without pulling in std::random_device on every call.
    uint64_t GenerateGUID() noexcept;
}
