#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kAssetMagic = 0x42484F50u;

struct alignas(16) AssetHeader {
    uint32_t magic = kAssetMagic;
    uint32_t version = 1;
    uint32_t typeHash = 0;
    uint32_t dataSize = 0;
    uint32_t checksum = 0;
    uint32_t pad[3]{};
};

static_assert(std::is_trivially_copyable_v<AssetHeader>);
static_assert(sizeof(AssetHeader) == 32);
static_assert(alignof(AssetHeader) == 16);

constexpr uint32_t assetHash(const char* name) {
    uint32_t hash = 2166136261u;
    if (name == nullptr) {
        return hash;
    }
    for (const char* p = name; *p != '\0'; ++p) {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*p));
        hash *= 16777619u;
    }
    return hash;
}

inline uint32_t assetHashBytes(const uint8_t* data, uint32_t size) {
    uint32_t hash = 2166136261u;
    if (data == nullptr) {
        return hash;
    }
    for (uint32_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

} // namespace burnhope
