#pragma once

#include "io/Blob.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

struct alignas(16) MaterialGpuData {
    float baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    uint32_t flags = 0;
    uint32_t albedoTexIndex = 0;
    uint32_t normalTexIndex = 0;
    uint32_t ormTexIndex = 0;
    uint32_t emissiveTexIndex = 0;
    uint32_t pad[4]{};
};

static_assert(std::is_trivially_copyable_v<MaterialGpuData>);
static_assert(sizeof(MaterialGpuData) == 64);
static_assert(alignof(MaterialGpuData) == 16);

inline size_t materialSerialize(const MaterialGpuData& mat, uint8_t* outBuf, size_t maxBytes) {
    constexpr size_t bytes = sizeof(AssetHeader) + sizeof(MaterialGpuData);
    if (outBuf == nullptr || maxBytes < bytes) {
        return 0;
    }
    AssetHeader header{};
    header.magic = kAssetMagic;
    header.version = 1;
    header.typeHash = assetHash("Material");
    header.dataSize = static_cast<uint32_t>(sizeof(MaterialGpuData));
    header.checksum = assetHashBytes(reinterpret_cast<const uint8_t*>(&mat), header.dataSize);
    std::memcpy(outBuf, &header, sizeof(header));
    std::memcpy(outBuf + sizeof(header), &mat, sizeof(mat));
    return bytes;
}

inline bool materialDeserialize(const uint8_t* data, size_t size, MaterialGpuData* outMat) {
    if (data == nullptr || outMat == nullptr || size < sizeof(AssetHeader) + sizeof(MaterialGpuData)) {
        return false;
    }
    AssetHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != kAssetMagic || header.version != 1 || header.typeHash != assetHash("Material")) {
        return false;
    }
    if (header.dataSize != sizeof(MaterialGpuData) || size < sizeof(AssetHeader) + header.dataSize) {
        return false;
    }
    const uint8_t* payload = data + sizeof(AssetHeader);
    if (header.checksum != assetHashBytes(payload, header.dataSize)) {
        return false;
    }
    std::memcpy(outMat, payload, sizeof(MaterialGpuData));
    return true;
}

} // namespace burnhope
