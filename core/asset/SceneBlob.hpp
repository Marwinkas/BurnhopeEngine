#pragma once

#include "gfx/Light.hpp"
#include "gfx/Material.hpp"
#include "gfx/Meshlet.hpp"
#include "io/Blob.hpp"
#include "io/File.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace burnhope {

// pad[1] low 28 bits. The lighter file is tagged so a frame can move it without a scene search.
constexpr uint32_t kSceneLighterGroup = 1u << 20;

struct SceneBlobInfo {
    uint32_t meshletCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t indexBytes = 0;
    uint32_t materialCount = 0;
    uint32_t textureCount = 0;
    uint32_t lightCount = 0;
};

struct SceneTexName {
    char file[64]{};
};

struct SceneBlobView {
    const SceneBlobInfo* info = nullptr;
    const Meshlet* meshlets = nullptr;
    const SceneVertex* vertices = nullptr;
    const uint32_t* indices = nullptr;
    const SceneInstance* instances = nullptr;
    const MaterialGpuData* materials = nullptr;
    const uint8_t* materialRaw = nullptr;
    uint32_t materialStride = 0;
    const SceneTexName* textures = nullptr;
    const LightGpuData* lights = nullptr;
};

static_assert(std::is_trivially_copyable_v<SceneVertex>);
static_assert(sizeof(SceneVertex) == 32);
static_assert(sizeof(SceneVertex) % 16 == 0);
static_assert(std::is_trivially_copyable_v<SceneInstance>);
static_assert(sizeof(SceneInstance) == 80);
static_assert(sizeof(SceneInstance) % 16 == 0);
static_assert(std::is_trivially_copyable_v<SceneBlobInfo>);
static_assert(sizeof(SceneBlobInfo) == 32);
static_assert(sizeof(SceneTexName) == 64);

constexpr uint32_t kSceneBlobTypeHash = assetHash("SceneBlob");

inline uint32_t sceneIndexBytes(uint32_t indexCount) {
    const uint32_t raw = indexCount * static_cast<uint32_t>(sizeof(uint32_t));
    return (raw + 15u) & ~15u;
}

inline bool sceneBlobView(const uint8_t* mapped, size_t size, SceneBlobView* out) {
    if (out == nullptr) {
        return false;
    }
    *out = {};
    if (mapped == nullptr || size < sizeof(AssetHeader) + sizeof(SceneBlobInfo)) {
        return false;
    }
    AssetHeader header{};
    std::memcpy(&header, mapped, sizeof(header));
    if (header.magic != kAssetMagic || (header.version != 9 && header.version != 10) || header.typeHash != kSceneBlobTypeHash) {
        return false;
    }
    if (size < sizeof(AssetHeader) + header.dataSize) {
        return false;
    }
    const uint8_t* payload = mapped + sizeof(AssetHeader);
    if (header.checksum != assetHashBytes(payload, header.dataSize)) {
        return false;
    }
    const auto* info = reinterpret_cast<const SceneBlobInfo*>(payload);
    const uint32_t matStride = header.version >= 10 ? static_cast<uint32_t>(sizeof(MaterialGpuData)) : kMaterialGpuV9;
    const size_t bytes = sizeof(SceneBlobInfo)
        + static_cast<size_t>(info->meshletCount) * sizeof(Meshlet)
        + static_cast<size_t>(info->vertexCount) * sizeof(SceneVertex)
        + info->indexBytes
        + static_cast<size_t>(info->instanceCount) * sizeof(SceneInstance)
        + static_cast<size_t>(info->materialCount) * matStride
        + static_cast<size_t>(info->textureCount) * sizeof(SceneTexName)
        + static_cast<size_t>(info->lightCount) * sizeof(LightGpuData);
    if (info->indexBytes < info->indexCount * sizeof(uint32_t) || bytes != header.dataSize) {
        return false;
    }
    const uint8_t* cursor = payload + sizeof(SceneBlobInfo);
    out->info = info;
    out->meshlets = reinterpret_cast<const Meshlet*>(cursor);
    cursor += static_cast<size_t>(info->meshletCount) * sizeof(Meshlet);
    out->vertices = reinterpret_cast<const SceneVertex*>(cursor);
    cursor += static_cast<size_t>(info->vertexCount) * sizeof(SceneVertex);
    out->indices = reinterpret_cast<const uint32_t*>(cursor);
    cursor += info->indexBytes;
    out->instances = reinterpret_cast<const SceneInstance*>(cursor);
    cursor += static_cast<size_t>(info->instanceCount) * sizeof(SceneInstance);
    out->materialRaw = cursor;
    out->materialStride = matStride;
    out->materials = matStride == sizeof(MaterialGpuData) ? reinterpret_cast<const MaterialGpuData*>(cursor) : nullptr;
    cursor += static_cast<size_t>(info->materialCount) * matStride;
    out->textures = reinterpret_cast<const SceneTexName*>(cursor);
    cursor += static_cast<size_t>(info->textureCount) * sizeof(SceneTexName);
    out->lights = info->lightCount > 0 ? reinterpret_cast<const LightGpuData*>(cursor) : nullptr;
    return true;
}

inline bool sceneBlobWrite(
    const char* path,
    const Meshlet* meshlets,
    uint32_t meshletCount,
    const SceneVertex* vertices,
    uint32_t vertexCount,
    const uint32_t* indices,
    uint32_t indexCount,
    const SceneInstance* instances,
    uint32_t instanceCount,
    const MaterialGpuData* materials = nullptr,
    uint32_t materialCount = 0,
    const SceneTexName* textures = nullptr,
    uint32_t textureCount = 0,
    const LightGpuData* lights = nullptr,
    uint32_t lightCount = 0) {
    if (path == nullptr || meshlets == nullptr || vertices == nullptr || indices == nullptr || instances == nullptr) {
        return false;
    }
    SceneBlobInfo info{};
    info.meshletCount = meshletCount;
    info.vertexCount = vertexCount;
    info.indexCount = indexCount;
    info.instanceCount = instanceCount;
    info.indexBytes = sceneIndexBytes(indexCount);
    info.materialCount = materialCount;
    info.textureCount = textureCount;
    info.lightCount = lightCount;
    const size_t payload = sizeof(SceneBlobInfo)
        + static_cast<size_t>(meshletCount) * sizeof(Meshlet)
        + static_cast<size_t>(vertexCount) * sizeof(SceneVertex)
        + info.indexBytes
        + static_cast<size_t>(instanceCount) * sizeof(SceneInstance)
        + static_cast<size_t>(materialCount) * sizeof(MaterialGpuData)
        + static_cast<size_t>(textureCount) * sizeof(SceneTexName)
        + static_cast<size_t>(lightCount) * sizeof(LightGpuData);
    if (payload > 0xffffffffu) {
        return false;
    }
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    AssetHeader header{};
    header.magic = kAssetMagic;
    header.version = 10;
    header.typeHash = kSceneBlobTypeHash;
    header.dataSize = static_cast<uint32_t>(payload);
    // The header leads the file but carries the payload checksum, so it is rewritten in place once the stream is hashed.
    uint32_t hash = 2166136261u;
    const auto put = [&](const void* data, size_t elem, size_t count) {
        if (count == 0) {
            return true;
        }
        hash = assetHashStep(hash, static_cast<const uint8_t*>(data), elem * count);
        return std::fwrite(data, elem, count, file) == count;
    };
    const uint8_t pad[16]{};
    const bool wrote = std::fwrite(&header, sizeof(header), 1, file) == 1
        && put(&info, sizeof(info), 1)
        && put(meshlets, sizeof(Meshlet), meshletCount)
        && put(vertices, sizeof(SceneVertex), vertexCount)
        && put(indices, sizeof(uint32_t), indexCount)
        && put(pad, 1, info.indexBytes - indexCount * sizeof(uint32_t))
        && put(instances, sizeof(SceneInstance), instanceCount)
        && put(materials, sizeof(MaterialGpuData), materialCount)
        && put(textures, sizeof(SceneTexName), textureCount)
        && put(lights, sizeof(LightGpuData), lightCount);
    header.checksum = hash;
    const bool stamped = wrote && std::fseek(file, 0, SEEK_SET) == 0 && std::fwrite(&header, sizeof(header), 1, file) == 1;
    const bool closed = std::fclose(file) == 0;
    return stamped && closed;
}

} // namespace burnhope
