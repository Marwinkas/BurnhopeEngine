#pragma once

#include "io/Blob.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

// Historical .bhop records are 64 bytes. The prefix below is that record.
constexpr uint32_t kMaterialGpuV9 = 64u;
constexpr uint32_t kMatBoxMap = 1u;
constexpr uint32_t kMatDoubleSided = 2u;
constexpr uint32_t kMatOrm = 4u;
constexpr uint32_t kMatMirror = 16u;
constexpr uint32_t kMatAlphaTest = 32u;
constexpr uint32_t kMatClearcoat = 64u;
constexpr uint32_t kMatWorldAligned = 256u;
constexpr uint32_t kMatFlipNormalY = 512u;
constexpr uint32_t kMatUnlit = 1024u;
constexpr uint32_t kMatClampUv = 2048u;

struct alignas(16) MaterialGpuData {
    float baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    uint32_t flags = kMatFlipNormalY;
    uint32_t albedoTexIndex = 0;
    uint32_t normalTexIndex = 0;
    uint32_t ormTexIndex = 0;
    uint32_t emissiveTexIndex = 0;
    uint32_t pad[4]{};
    float reflectance = 0.5f;
    float aoStrength = 1.0f;
    float normalScale = 1.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.04f;
    float alphaCutoff = 0.5f;
    float uvTiling[2]{1.0f, 1.0f};
    float uvOffset[2]{0.0f, 0.0f};
    float uvRotation = 0.0f;
    float worldGridSize = 1.0f;
    float triplanarSharpness = 4.0f;
    float worldOffset[3]{0.0f, 0.0f, 0.0f};
    float emissiveColor[3]{1.0f, 1.0f, 1.0f};
    float emissiveGain = 0.0f;
    uint32_t aoTex = 0;
    uint32_t tail[11]{};
};

static_assert(std::is_trivially_copyable_v<MaterialGpuData>);
static_assert(sizeof(MaterialGpuData) == 192);
static_assert(sizeof(MaterialGpuData) % 64 == 0);
static_assert(alignof(MaterialGpuData) == 16);
static_assert(offsetof(MaterialGpuData, roughness) == 16);
static_assert(offsetof(MaterialGpuData, pad) == 48);
static_assert(offsetof(MaterialGpuData, reflectance) == 64);
static_assert(offsetof(MaterialGpuData, uvTiling) == 88);
static_assert(offsetof(MaterialGpuData, worldOffset) == 116);
static_assert(offsetof(MaterialGpuData, emissiveColor) == 128);
static_assert(offsetof(MaterialGpuData, aoTex) == 144);

// Authoring document for a .bhmat file. The first fields match the historical document.
struct BhmatDoc {
    uint32_t version = 1;
    float base[3]{1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissive = 0.0f;
    float albedoGain = 1.0f;
    float normalGain = 1.0f;
    float ormGain = 1.0f;
    float emissiveGain = 0.0f;
    char albedoPath[260]{};
    char normalPath[260]{};
    char ormPath[260]{};
    char emissivePath[260]{};
    float baseA = 1.0f;
    float reflectance = 0.5f;
    float aoStrength = 1.0f;
    float normalScale = 1.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.04f;
    float alphaCutoff = 0.5f;
    float uvTiling[2]{1.0f, 1.0f};
    float uvOffset[2]{0.0f, 0.0f};
    float uvRotation = 0.0f;
    float worldGridSize = 1.0f;
    float triplanarSharpness = 4.0f;
    float worldOffset[3]{0.0f, 0.0f, 0.0f};
    float emissiveColor[3]{1.0f, 1.0f, 1.0f};
    char roughPath[260]{};
    char metalPath[260]{};
    char aoPath[260]{};
    char heightPath[260]{};
    char file[260]{};
    float heightScale = 0.0f;
    uint8_t worldAligned = 0;
    uint8_t doubleSided = 0;
    uint8_t alphaCut = 0;
    uint8_t flipNormalY = 1;
    uint8_t unlit = 0;
    uint8_t repeat = 1;
    uint8_t padDoc[2]{};
};

static_assert(std::is_trivially_copyable_v<BhmatDoc>);

inline void materialAdoptRecord(MaterialGpuData& dst, const void* src, uint32_t stride) {
    dst = {};
    if (src == nullptr || stride == 0u) {
        return;
    }
    const uint32_t n = stride < sizeof(MaterialGpuData) ? stride : static_cast<uint32_t>(sizeof(MaterialGpuData));
    std::memcpy(&dst, src, n);
    if (stride <= kMaterialGpuV9) {
        dst.flags |= kMatFlipNormalY;
        float packed = 0.0f;
        std::memcpy(&packed, &dst.pad[1], sizeof(float));
        if (packed >= 0.01f && packed <= 1.0f) {
            dst.reflectance = packed;
        }
        if ((dst.flags & kMatClearcoat) != 0u && dst.clearcoat <= 0.0f) {
            dst.clearcoat = 1.0f;
        }
        if (dst.alphaCutoff <= 0.0f) {
            dst.alphaCutoff = 0.5f;
        }
        if (dst.uvTiling[0] == 0.0f && dst.uvTiling[1] == 0.0f) {
            dst.uvTiling[0] = 1.0f;
            dst.uvTiling[1] = 1.0f;
        }
        if (dst.worldGridSize <= 0.0f) {
            dst.worldGridSize = 1.0f;
        }
        if (dst.triplanarSharpness < 1.0f) {
            dst.triplanarSharpness = 4.0f;
        }
        if (dst.normalScale <= 0.0f) {
            dst.normalScale = 1.0f;
        }
        if (dst.aoStrength <= 0.0f) {
            dst.aoStrength = 1.0f;
        }
        if (dst.emissiveIntensity > 0.0f) {
            dst.emissiveColor[0] = dst.baseColor[0];
            dst.emissiveColor[1] = dst.baseColor[1];
            dst.emissiveColor[2] = dst.baseColor[2];
            dst.emissiveGain = dst.emissiveIntensity;
        }
        if (dst.emissiveColor[0] == 0.0f && dst.emissiveColor[1] == 0.0f && dst.emissiveColor[2] == 0.0f) {
            dst.emissiveColor[0] = dst.emissiveColor[1] = dst.emissiveColor[2] = 1.0f;
        }
    }
}

inline void materialGpuFromDoc(MaterialGpuData& gpu, const BhmatDoc& doc) {
    gpu.baseColor[0] = doc.base[0];
    gpu.baseColor[1] = doc.base[1];
    gpu.baseColor[2] = doc.base[2];
    gpu.baseColor[3] = doc.baseA;
    gpu.roughness = doc.roughness;
    gpu.metallic = doc.metallic;
    gpu.emissiveIntensity = doc.emissive;
    gpu.reflectance = doc.reflectance;
    std::memcpy(&gpu.pad[1], &doc.reflectance, sizeof(float));
    gpu.aoStrength = doc.aoStrength;
    gpu.normalScale = doc.normalScale;
    gpu.clearcoat = doc.clearcoat;
    gpu.clearcoatRoughness = doc.clearcoatRoughness;
    gpu.alphaCutoff = doc.alphaCutoff;
    gpu.uvTiling[0] = doc.uvTiling[0];
    gpu.uvTiling[1] = doc.uvTiling[1];
    gpu.uvOffset[0] = doc.uvOffset[0];
    gpu.uvOffset[1] = doc.uvOffset[1];
    gpu.uvRotation = doc.uvRotation;
    gpu.worldGridSize = doc.worldGridSize;
    gpu.triplanarSharpness = doc.triplanarSharpness;
    gpu.worldOffset[0] = doc.worldOffset[0];
    gpu.worldOffset[1] = doc.worldOffset[1];
    gpu.worldOffset[2] = doc.worldOffset[2];
    gpu.emissiveColor[0] = doc.emissiveColor[0];
    gpu.emissiveColor[1] = doc.emissiveColor[1];
    gpu.emissiveColor[2] = doc.emissiveColor[2];
    gpu.emissiveGain = doc.emissiveGain;
    uint32_t flags = gpu.flags & (kMatBoxMap | kMatOrm | kMatMirror);
    if (doc.doubleSided != 0) {
        flags |= kMatDoubleSided;
    }
    if (doc.alphaCut != 0) {
        flags |= kMatAlphaTest;
    }
    if (doc.clearcoat > 0.001f) {
        flags |= kMatClearcoat;
    }
    if (doc.worldAligned != 0) {
        flags |= kMatWorldAligned;
    }
    if (doc.flipNormalY != 0) {
        flags |= kMatFlipNormalY;
    }
    if (doc.unlit != 0) {
        flags |= kMatUnlit;
    }
    if (doc.repeat == 0) {
        flags |= kMatClampUv;
    }
    gpu.flags = flags;
    std::memcpy(&gpu.tail[0], &doc.heightScale, sizeof(float));
}

inline void materialDocFromGpu(BhmatDoc& doc, const MaterialGpuData& gpu) {
    doc.base[0] = gpu.baseColor[0];
    doc.base[1] = gpu.baseColor[1];
    doc.base[2] = gpu.baseColor[2];
    doc.baseA = gpu.baseColor[3];
    doc.roughness = gpu.roughness;
    doc.metallic = gpu.metallic;
    doc.emissive = gpu.emissiveIntensity;
    doc.reflectance = gpu.reflectance;
    doc.aoStrength = gpu.aoStrength;
    doc.normalScale = gpu.normalScale;
    doc.clearcoat = gpu.clearcoat;
    doc.clearcoatRoughness = gpu.clearcoatRoughness;
    doc.alphaCutoff = gpu.alphaCutoff;
    doc.uvTiling[0] = gpu.uvTiling[0];
    doc.uvTiling[1] = gpu.uvTiling[1];
    doc.uvOffset[0] = gpu.uvOffset[0];
    doc.uvOffset[1] = gpu.uvOffset[1];
    doc.uvRotation = gpu.uvRotation;
    doc.worldGridSize = gpu.worldGridSize;
    doc.triplanarSharpness = gpu.triplanarSharpness;
    doc.worldOffset[0] = gpu.worldOffset[0];
    doc.worldOffset[1] = gpu.worldOffset[1];
    doc.worldOffset[2] = gpu.worldOffset[2];
    doc.emissiveColor[0] = gpu.emissiveColor[0];
    doc.emissiveColor[1] = gpu.emissiveColor[1];
    doc.emissiveColor[2] = gpu.emissiveColor[2];
    doc.emissiveGain = gpu.emissiveGain;
    doc.doubleSided = (gpu.flags & kMatDoubleSided) != 0u ? 1u : 0u;
    doc.alphaCut = (gpu.flags & kMatAlphaTest) != 0u ? 1u : 0u;
    doc.worldAligned = (gpu.flags & kMatWorldAligned) != 0u ? 1u : 0u;
    doc.flipNormalY = (gpu.flags & kMatFlipNormalY) != 0u ? 1u : 0u;
    doc.unlit = (gpu.flags & kMatUnlit) != 0u ? 1u : 0u;
    doc.repeat = (gpu.flags & kMatClampUv) != 0u ? 0u : 1u;
    std::memcpy(&doc.heightScale, &gpu.tail[0], sizeof(float));
    if (doc.clearcoat <= 0.0f && (gpu.flags & kMatClearcoat) != 0u) {
        doc.clearcoat = 1.0f;
    }
}

[[nodiscard]] bool materialLoadBhmat(const char* path, BhmatDoc& doc);
[[nodiscard]] bool materialSaveBhmat(const char* path, const BhmatDoc& doc);

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
    if (data == nullptr || outMat == nullptr || size < sizeof(AssetHeader) + kMaterialGpuV9) {
        return false;
    }
    AssetHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != kAssetMagic || header.version != 1 || header.typeHash != assetHash("Material")) {
        return false;
    }
    const bool legacy = header.dataSize == kMaterialGpuV9;
    const bool wide = header.dataSize == sizeof(MaterialGpuData);
    if ((!legacy && !wide) || size < sizeof(AssetHeader) + header.dataSize) {
        return false;
    }
    const uint8_t* payload = data + sizeof(AssetHeader);
    if (header.checksum != assetHashBytes(payload, header.dataSize)) {
        return false;
    }
    materialAdoptRecord(*outMat, payload, header.dataSize);
    return true;
}

} // namespace burnhope
