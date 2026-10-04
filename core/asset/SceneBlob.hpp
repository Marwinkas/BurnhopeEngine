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

struct SceneVertex {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float u = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float v = 0.0f;
};

struct SceneInstance {
    float world[12]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    float sphere[4]{};
    uint32_t materialId = 0;
    uint32_t meshId = 0;
    uint32_t pad[2]{};
};

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
    if (header.magic != kAssetMagic || header.version != 9 || header.typeHash != assetHash("BistroScene")) {
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
    const size_t bytes = sizeof(SceneBlobInfo)
        + static_cast<size_t>(info->meshletCount) * sizeof(Meshlet)
        + static_cast<size_t>(info->vertexCount) * sizeof(SceneVertex)
        + info->indexBytes
        + static_cast<size_t>(info->instanceCount) * sizeof(SceneInstance)
        + static_cast<size_t>(info->materialCount) * sizeof(MaterialGpuData)
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
    out->materials = reinterpret_cast<const MaterialGpuData*>(cursor);
    cursor += static_cast<size_t>(info->materialCount) * sizeof(MaterialGpuData);
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
    header.version = 9;
    header.typeHash = assetHash("BistroScene");
    header.dataSize = static_cast<uint32_t>(payload);
    const bool wrote = std::fwrite(&header, sizeof(header), 1, file) == 1
        && std::fwrite(&info, sizeof(info), 1, file) == 1
        && std::fwrite(meshlets, sizeof(Meshlet), meshletCount, file) == meshletCount
        && std::fwrite(vertices, sizeof(SceneVertex), vertexCount, file) == vertexCount
        && std::fwrite(indices, sizeof(uint32_t), indexCount, file) == indexCount;
    if (wrote && info.indexBytes > indexCount * sizeof(uint32_t)) {
        const uint8_t pad[16]{};
        if (std::fwrite(pad, info.indexBytes - indexCount * sizeof(uint32_t), 1, file) != 1) {
            std::fclose(file);
            return false;
        }
    }
    if (!wrote || std::fwrite(instances, sizeof(SceneInstance), instanceCount, file) != instanceCount) {
        std::fclose(file);
        return false;
    }
    if (materialCount > 0 && std::fwrite(materials, sizeof(MaterialGpuData), materialCount, file) != materialCount) {
        std::fclose(file);
        return false;
    }
    if (textureCount > 0 && std::fwrite(textures, sizeof(SceneTexName), textureCount, file) != textureCount) {
        std::fclose(file);
        return false;
    }
    if (lightCount > 0 && std::fwrite(lights, sizeof(LightGpuData), lightCount, file) != lightCount) {
        std::fclose(file);
        return false;
    }
    std::fclose(file);
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        return false;
    }
    header.checksum = assetHashBytes(mapped.data + sizeof(AssetHeader), header.dataSize);
    fileUnmap(&mapped);
    file = std::fopen(path, "rb+");
    if (file == nullptr) {
        return false;
    }
    const bool stamped = std::fwrite(&header, sizeof(header), 1, file) == 1;
    std::fclose(file);
    return stamped;
}

} // namespace burnhope
