#pragma once

#include "io/Blob.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

struct MeshletVertex {
    float pos[3]{};
    uint32_t packedNormal = 0;
    float uv[2]{};
    uint32_t packedTangent = 0;
    uint32_t pad = 0;
};

struct Meshlet {
    float center[3]{};
    float radius = 0.0f;
    float coneAxis[3]{};
    float coneCutoff = 1.0f;
    uint32_t vertexOffset = 0;
    uint32_t triangleOffset = 0;
    uint8_t vertexCount = 0;
    uint8_t triangleCount = 0;
    uint16_t reserved0 = 0;
    uint32_t reserved1 = 0;
};

static_assert(std::is_trivially_copyable_v<MeshletVertex>);
static_assert(sizeof(MeshletVertex) == 32);
static_assert(sizeof(MeshletVertex) % 16 == 0);
static_assert(std::is_trivially_copyable_v<Meshlet>);
static_assert(sizeof(Meshlet) == 48);
static_assert(sizeof(Meshlet) % 16 == 0);

inline uint32_t meshletPackNormal(float x, float y, float z) {
    const float l1 = std::abs(x) + std::abs(y) + std::abs(z);
    if (l1 < 1.0e-6f) {
        return 0;
    }
    float ox = x / l1;
    float oy = y / l1;
    if (z < 0.0f) {
        const float tx = (1.0f - std::abs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
        const float ty = (1.0f - std::abs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
        ox = tx;
        oy = ty;
    }
    const auto snorm = [](float v) {
        int q = static_cast<int>(std::lround(v * 32767.0f));
        return static_cast<uint16_t>(static_cast<int16_t>(std::clamp(q, -32767, 32767)));
    };
    return static_cast<uint32_t>(snorm(ox)) | (static_cast<uint32_t>(snorm(oy)) << 16);
}

inline void meshletUnpackNormal(uint32_t packed, float& x, float& y, float& z) {
    const auto snorm = [](uint32_t bits) {
        return static_cast<float>(static_cast<int16_t>(static_cast<uint16_t>(bits))) / 32767.0f;
    };
    x = snorm(packed & 0xffffu);
    y = snorm(packed >> 16);
    z = 1.0f - std::abs(x) - std::abs(y);
    if (z < 0.0f) {
        const float tx = (1.0f - std::abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float ty = (1.0f - std::abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = tx;
        y = ty;
    }
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1.0e-6f) {
        x = 0.0f;
        y = 0.0f;
        z = 1.0f;
        return;
    }
    const float inv = 1.0f / len;
    x *= inv;
    y *= inv;
    z *= inv;
}

inline void meshletFit(Meshlet& meshlet, const MeshletVertex* verts, uint32_t count) {
    meshlet.center[0] = meshlet.center[1] = meshlet.center[2] = 0.0f;
    meshlet.radius = 0.0f;
    meshlet.coneAxis[0] = 0.0f;
    meshlet.coneAxis[1] = 0.0f;
    meshlet.coneAxis[2] = 1.0f;
    meshlet.coneCutoff = -1.0f;
    meshlet.vertexCount = static_cast<uint8_t>(count > 255u ? 255u : count);
    if (verts == nullptr || count == 0) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        meshlet.center[0] += verts[i].pos[0];
        meshlet.center[1] += verts[i].pos[1];
        meshlet.center[2] += verts[i].pos[2];
    }
    const float inv = 1.0f / static_cast<float>(count);
    meshlet.center[0] *= inv;
    meshlet.center[1] *= inv;
    meshlet.center[2] *= inv;
    for (uint32_t i = 0; i < count; ++i) {
        const float dx = verts[i].pos[0] - meshlet.center[0];
        const float dy = verts[i].pos[1] - meshlet.center[1];
        const float dz = verts[i].pos[2] - meshlet.center[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > meshlet.radius * meshlet.radius) {
            meshlet.radius = std::sqrt(d2);
        }
    }
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    uint32_t normalCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (verts[i].packedNormal == 0) {
            continue;
        }
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        meshletUnpackNormal(verts[i].packedNormal, nx, ny, nz);
        ax += nx;
        ay += ny;
        az += nz;
        normalCount += 1;
    }
    if (normalCount == 0) {
        return;
    }
    const float al = std::sqrt(ax * ax + ay * ay + az * az);
    if (al <= 1.0e-6f) {
        return;
    }
    meshlet.coneAxis[0] = ax / al;
    meshlet.coneAxis[1] = ay / al;
    meshlet.coneAxis[2] = az / al;
    float cutoff = 1.0f;
    for (uint32_t i = 0; i < count; ++i) {
        if (verts[i].packedNormal == 0) {
            continue;
        }
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        meshletUnpackNormal(verts[i].packedNormal, nx, ny, nz);
        const float dot = nx * meshlet.coneAxis[0] + ny * meshlet.coneAxis[1] + nz * meshlet.coneAxis[2];
        if (dot < cutoff) {
            cutoff = dot;
        }
    }
    meshlet.coneCutoff = cutoff;
}

inline bool meshletConeVisible(const Meshlet& meshlet, float eyeX, float eyeY, float eyeZ) {
    float vx = eyeX - meshlet.center[0];
    float vy = eyeY - meshlet.center[1];
    float vz = eyeZ - meshlet.center[2];
    const float vl = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (vl < 1.0e-6f) {
        return true;
    }
    vx /= vl;
    vy /= vl;
    vz /= vl;
    const float facing = vx * meshlet.coneAxis[0] + vy * meshlet.coneAxis[1] + vz * meshlet.coneAxis[2];
    const float c = meshlet.coneCutoff < 0.0f ? 0.0f : (meshlet.coneCutoff > 1.0f ? 1.0f : meshlet.coneCutoff);
    const float s2 = 1.0f - c * c;
    const float s = std::sqrt(s2 > 0.0f ? s2 : 0.0f);
    return facing >= -s;
}

inline const Meshlet* meshletView(const uint8_t* mapped, size_t size, uint32_t& count) {
    count = 0;
    if (mapped == nullptr || size < sizeof(AssetHeader)) {
        return nullptr;
    }
    AssetHeader header{};
    std::memcpy(&header, mapped, sizeof(header));
    if (header.magic != kAssetMagic || header.version != 1 || header.typeHash != assetHash("MeshletBlob")) {
        return nullptr;
    }
    if (header.dataSize == 0 || header.dataSize % sizeof(Meshlet) != 0) {
        return nullptr;
    }
    if (size < sizeof(AssetHeader) + header.dataSize) {
        return nullptr;
    }
    const uint8_t* payload = mapped + sizeof(AssetHeader);
    if (header.checksum != assetHashBytes(payload, header.dataSize)) {
        return nullptr;
    }
    count = header.dataSize / static_cast<uint32_t>(sizeof(Meshlet));
    return reinterpret_cast<const Meshlet*>(payload);
}

inline size_t meshletSerialize(const Meshlet* meshlets, uint32_t count, uint8_t* outBuf, size_t maxBytes) {
    const size_t payload = static_cast<size_t>(count) * sizeof(Meshlet);
    const size_t bytes = sizeof(AssetHeader) + payload;
    if (meshlets == nullptr || outBuf == nullptr || maxBytes < bytes) {
        return 0;
    }
    AssetHeader header{};
    header.magic = kAssetMagic;
    header.version = 1;
    header.typeHash = assetHash("MeshletBlob");
    header.dataSize = static_cast<uint32_t>(payload);
    header.checksum = assetHashBytes(reinterpret_cast<const uint8_t*>(meshlets), header.dataSize);
    std::memcpy(outBuf, &header, sizeof(header));
    std::memcpy(outBuf + sizeof(header), meshlets, payload);
    return bytes;
}

} // namespace burnhope
