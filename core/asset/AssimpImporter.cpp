#include "io/File.hpp"
#include "asset/AssimpImporter.hpp"

#include "asset/SceneBlob.hpp"
#include "gfx/Light.hpp"
#include "gfx/MeshletBuilder.hpp"
#include "platform/PlatformUtil.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace burnhope {
namespace {

const char* fileNameOf(const char* path) {
    if (path == nullptr) {
        return "";
    }
    const char* slash = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            slash = p + 1;
        }
    }
    return slash;
}

struct TexSearch {
    char modelDir[320]{};
    bool hasModelDir = false;
};

void texSearchFromFbx(const char* fbxPath, TexSearch& out) {
    out.hasModelDir = pathParent(fbxPath, out.modelDir, sizeof(out.modelDir));
}

// Склейка dir/mid/file. Усечённый путь не проверяют: он указал бы на другой файл.
bool texReadableAt(const char* dir, std::string_view mid, const char* file) {
    char path[320];
    const auto res = std::format_to_n(path, sizeof(path) - 1, "{}/{}{}", dir, mid, file);
    if (static_cast<size_t>(res.size) >= sizeof(path)) {
        return false;
    }
    *res.out = '\0';
    return fileExists(path);
}

bool texOnDisk(const TexSearch& search, const char* file) {
    if (file == nullptr || file[0] == '\0') {
        return false;
    }
    if (fileExists(file)) {
        return true;
    }
    if (!search.hasModelDir) {
        return false;
    }
    const char* base = fileNameOf(file);
    return texReadableAt(search.modelDir, "", file)
        || texReadableAt(search.modelDir, "", base)
        || texReadableAt(search.modelDir, "Textures/", base)
        || texReadableAt(search.modelDir, "Textures/", file);
}

void siblingFile(const char* albedo, const char* kind, char* out, size_t cap) {
    out[0] = '\0';
    if (albedo == nullptr || kind == nullptr || cap < 8) {
        return;
    }
    const char* hit = std::strstr(albedo, "BaseColor");
    const char* token = "BaseColor";
    if (hit == nullptr) {
        hit = std::strstr(albedo, "_Color");
        token = "_Color";
    }
    if (hit != nullptr) {
        const size_t head = static_cast<size_t>(hit - albedo);
        std::snprintf(out, cap, "%.*s%s%s", static_cast<int>(head), albedo, kind, hit + std::strlen(token));
        return;
    }
    const char* dot = std::strrchr(albedo, '.');
    if (dot == nullptr) {
        std::snprintf(out, cap, "%s_%s.dds", albedo, kind);
        return;
    }
    const size_t head = static_cast<size_t>(dot - albedo);
    std::snprintf(out, cap, "%.*s_%s%s", static_cast<int>(head), albedo, kind, dot);
}

uint32_t textureSlot(std::vector<SceneTexName>& names, const char* path) {
    const char* file = fileNameOf(path);
    if (file[0] == '\0') {
        return 0;
    }
    for (uint32_t i = 1; i < names.size(); ++i) {
        if (std::strcmp(names[i].file, file) == 0) {
            return i;
        }
    }
    if (names.size() >= 768) {
        return 0;
    }
    SceneTexName name{};
    std::snprintf(name.file, sizeof(name.file), "%s", file);
    names.push_back(name);
    return static_cast<uint32_t>(names.size() - 1);
}
void repairMesh(std::vector<float>& xyz, std::vector<float>& nrm, std::vector<uint32_t>& idx, bool flipWinding) {
    const uint32_t vertCount = static_cast<uint32_t>(xyz.size() / 3u);
    const uint32_t triCount = static_cast<uint32_t>(idx.size() / 3u);
    if (vertCount == 0 || triCount == 0 || nrm.size() < xyz.size()) {
        idx.clear();
        return;
    }
    bool authored = false;
    for (uint32_t v = 0; v < vertCount; ++v) {
        const float x = nrm[v * 3u];
        const float y = nrm[v * 3u + 1u];
        const float z = nrm[v * 3u + 2u];
        if (x * x + y * y + z * z > 1.0e-8f) {
            authored = true;
            break;
        }
    }
    std::vector<float> face(nrm.size(), 0.0f);
    std::vector<uint32_t> kept;
    kept.reserve(idx.size());

    for (uint32_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = idx[static_cast<size_t>(t) * 3u];
        const uint32_t i1 = idx[static_cast<size_t>(t) * 3u + (flipWinding ? 2u : 1u)];
        const uint32_t i2 = idx[static_cast<size_t>(t) * 3u + (flipWinding ? 1u : 2u)];
        if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount || i0 == i1 || i1 == i2 || i2 == i0) {
            continue;
        }
        const float e1x = xyz[i1 * 3u] - xyz[i0 * 3u];
        const float e1y = xyz[i1 * 3u + 1u] - xyz[i0 * 3u + 1u];
        const float e1z = xyz[i1 * 3u + 2u] - xyz[i0 * 3u + 2u];
        const float e2x = xyz[i2 * 3u] - xyz[i0 * 3u];
        const float e2y = xyz[i2 * 3u + 1u] - xyz[i0 * 3u + 1u];
        const float e2z = xyz[i2 * 3u + 2u] - xyz[i0 * 3u + 2u];
        const float nx = e1y * e2z - e1z * e2y;
        const float ny = e1z * e2x - e1x * e2z;
        const float nz = e1x * e2y - e1y * e2x;
        const float area2 = nx * nx + ny * ny + nz * nz;
        if (area2 <= 1.0e-28f) {
            continue;
        }
        face[i0 * 3u] += nx;
        face[i0 * 3u + 1u] += ny;
        face[i0 * 3u + 2u] += nz;
        face[i1 * 3u] += nx;
        face[i1 * 3u + 1u] += ny;
        face[i1 * 3u + 2u] += nz;
        face[i2 * 3u] += nx;
        face[i2 * 3u + 1u] += ny;
        face[i2 * 3u + 2u] += nz;

        kept.push_back(i0);
        kept.push_back(i1);
        kept.push_back(i2);
    }

    if (!authored) {
        nrm.swap(face);
    }
    for (uint32_t v = 0; v < vertCount; ++v) {
        float x = nrm[v * 3u];
        float y = nrm[v * 3u + 1u];
        float z = nrm[v * 3u + 2u];
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1.0e-8f) {
            // Если у отдельной вершины нормаль была нулевой — берем геометрическую нормаль из face
            x = face[v * 3u];
            y = face[v * 3u + 1u];
            z = face[v * 3u + 2u];
            len = std::sqrt(x * x + y * y + z * z);
        }
        if (len < 1.0e-8f) {
            nrm[v * 3u] = 0.0f;
            nrm[v * 3u + 1u] = 1.0f;
            nrm[v * 3u + 2u] = 0.0f;
        } else {
            nrm[v * 3u] = x / len;
            nrm[v * 3u + 1u] = y / len;
            nrm[v * 3u + 2u] = z / len;
        }
    }
    idx.swap(kept);
}
void bakePoint(const aiMatrix4x4& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m.a1 * x + m.a2 * y + m.a3 * z + m.a4;
    oy = m.b1 * x + m.b2 * y + m.b3 * z + m.b4;
    oz = m.c1 * x + m.c2 * y + m.c3 * z + m.c4;
}
bool matrixFlipsWinding(const aiMatrix4x4& m) {
    const float c00 = m.b2 * m.c3 - m.b3 * m.c2;
    const float c01 = m.b3 * m.c1 - m.b1 * m.c3;
    const float c02 = m.b1 * m.c2 - m.b2 * m.c1;
    const float det = m.a1 * c00 + m.a2 * c01 + m.a3 * c02;
    return det < 0.0f;
}

void bakeNormal(const aiMatrix4x4& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    const float c00 = m.b2 * m.c3 - m.b3 * m.c2;
    const float c01 = m.b3 * m.c1 - m.b1 * m.c3;
    const float c02 = m.b1 * m.c2 - m.b2 * m.c1;
    const float c10 = m.a3 * m.c2 - m.a2 * m.c3;
    const float c11 = m.a1 * m.c3 - m.a3 * m.c1;
    const float c12 = m.a2 * m.c1 - m.a1 * m.c2;
    const float c20 = m.a2 * m.b3 - m.a3 * m.b2;
    const float c21 = m.a3 * m.b1 - m.a1 * m.b3;
    const float c22 = m.a1 * m.b2 - m.a2 * m.b1;
    const float det = m.a1 * c00 + m.a2 * c01 + m.a3 * c02;
    const float sign = det < 0.0f ? -1.0f : 1.0f;

    ox = (c00 * x + c01 * y + c02 * z) * sign;
    oy = (c10 * x + c11 * y + c12 * z) * sign;
    oz = (c20 * x + c21 * y + c22 * z) * sign;
    const float len = std::sqrt(ox * ox + oy * oy + oz * oz);
    if (len < 1.0e-8f) {
        ox = 0.0f;
        oy = 1.0f;
        oz = 0.0f;
        return;
    }
    ox /= len;
    oy /= len;
    oz /= len;
}

bool nameHas(const char* text, const char* needle) {
    if (text == nullptr || needle == nullptr) {
        return false;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        const char* a = p;
        const char* b = needle;
        while (*a != '\0' && *b != '\0' && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

bool foliageMaterial(const aiMaterial* mat) {
    if (mat == nullptr) {
        return false;
    }
    aiString mode;
    if (mat->Get(AI_MATKEY_GLTF_ALPHAMODE, mode) == AI_SUCCESS
        && (std::strcmp(mode.C_Str(), "MASK") == 0 || std::strcmp(mode.C_Str(), "BLEND") == 0)) {
        return true;
    }
    aiString name;
    mat->Get(AI_MATKEY_NAME, name);
    if (nameHas(name.C_Str(), "leaf") || nameHas(name.C_Str(), "ivy") || nameHas(name.C_Str(), "foliage")
        || nameHas(name.C_Str(), "frond") || nameHas(name.C_Str(), "needle")) {
        return true;
    }
    aiString tex;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex) == AI_SUCCESS || mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) == AI_SUCCESS) {
        if (nameHas(tex.C_Str(), "leaf") || nameHas(tex.C_Str(), "ivy") || nameHas(tex.C_Str(), "foliage")) {
            return true;
        }
    }
    return false;
}

bool lightNameMatch(const char* node, const char* light, bool prefix) {
    if (node == nullptr || light == nullptr) {
        return false;
    }
    const size_t n = std::strlen(light);
    if (std::strncmp(node, light, n) != 0) {
        return false;
    }
    if (node[n] == '\0') {
        return true;
    }
    if (!prefix) {
        return false;
    }
    for (const char* p = node + n; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return node[n] != '\0';
}

void collectLightNodes(const aiNode* node, const aiMatrix4x4& parent, const char* light, bool prefix, std::vector<aiMatrix4x4>& hits) {
    if (node == nullptr) {
        return;
    }
    const aiMatrix4x4 world = parent * node->mTransformation;
    if (lightNameMatch(node->mName.C_Str(), light, prefix)) {
        hits.push_back(world);
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c) {
        collectLightNodes(node->mChildren[c], world, light, prefix, hits);
    }
}

void pushLight(std::vector<LightGpuData>& lights, const aiLight* src, const aiVector3D& pos, const aiVector3D& dir, float radiusOverride) {
    if (src == nullptr || lights.size() >= kLightCap) {
        return;
    }
    LightGpuData lamp{};
    lamp.pos[0] = pos.x;
    lamp.pos[1] = pos.y;
    lamp.pos[2] = pos.z;
    lamp.color[0] = src->mColorDiffuse.r;
    lamp.color[1] = src->mColorDiffuse.g;
    lamp.color[2] = src->mColorDiffuse.b;
    const float peak = std::max(lamp.color[0], std::max(lamp.color[1], lamp.color[2]));
    if (peak > 1.0f) {
        lamp.color[0] /= peak;
        lamp.color[1] /= peak;
        lamp.color[2] /= peak;
    }
    lamp.intensity = 1.2f;
    lamp.direction[0] = dir.x;
    lamp.direction[1] = dir.y;
    lamp.direction[2] = dir.z;
    lamp.shadowMapIndex = -1;
    if (src->mType == aiLightSource_DIRECTIONAL) {
        lamp.type = kLightDirectional;
        lamp.radius = 0.0f;
    } else if (src->mType == aiLightSource_SPOT) {
        lamp.type = kLightSpot;
        lamp.spotAngle = src->mAngleOuterCone > 0.01f ? src->mAngleOuterCone : 0.6f;
        lamp.radius = radiusOverride > 0.0f ? radiusOverride : 25.0f;
    } else {
        lamp.type = kLightPoint;
        lamp.radius = radiusOverride > 0.0f ? radiusOverride : (src->mSize.x > 0.05f ? src->mSize.x : 20.0f);
    }
    lights.push_back(lamp);
}

aiMatrix4x4 nodeWorld(const aiNode* node, const aiMatrix4x4& parent, const aiString& name, bool& found) {
    const aiMatrix4x4 world = parent * node->mTransformation;
    if (node->mName == name) {
        found = true;
        return world;
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c) {
        bool child = false;
        const aiMatrix4x4 nested = nodeWorld(node->mChildren[c], world, name, child);
        if (child) {
            found = true;
            return nested;
        }
    }
    found = false;
    return world;
}

bool appendSceneMeshes(
    const aiScene* scene,
    const char* label,
    const TexSearch& texSearch,
    std::vector<MaterialGpuData>& materials,
    std::vector<SceneTexName>& textures,
    std::vector<LightGpuData>& lights,
    MeshletBuild& build) {
    const bool hand = nameHas(label, "lighter");
    const uint32_t groupBase = hand ? kSceneLighterGroup : 0u;
    const uint32_t materialBase = static_cast<uint32_t>(materials.size());
    const uint32_t localCount = scene->mNumMaterials > 0 ? scene->mNumMaterials : 1u;
    materials.resize(materialBase + localCount);
    for (uint32_t i = 0; i < localCount; ++i) {
        MaterialGpuData& dst = materials[materialBase + i];
        dst.baseColor[0] = dst.baseColor[1] = dst.baseColor[2] = dst.baseColor[3] = 1.0f;
        dst.roughness = 0.5f;
        if (scene->mMaterials == nullptr || i >= scene->mNumMaterials) {
            continue;
        }
        const aiMaterial* mat = scene->mMaterials[i];
        aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
        if (mat->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS) {
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, color);
        }
        dst.baseColor[0] = color.r;
        dst.baseColor[1] = color.g;
        dst.baseColor[2] = color.b;
        dst.baseColor[3] = color.a;
        float rough = 0.5f;
        if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough) == AI_SUCCESS) {
            dst.roughness = rough;
        }
        float metal = 0.0f;
        if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metal) == AI_SUCCESS) {
            dst.metallic = metal;
        }
        aiString tex;
        if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) != AI_SUCCESS) {
            mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex);
        }
        dst.albedoTexIndex = textureSlot(textures, tex.C_Str());
        aiTextureMapping mapping = aiTextureMapping_UV;
        mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex, &mapping);
        if (mapping == aiTextureMapping_BOX || mapping == aiTextureMapping_OTHER) {
            dst.flags |= 1u;
        }
        int twoSided = 0;
        if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS && twoSided != 0) {
            dst.flags |= 2u;
        }
        if (foliageMaterial(mat)) {
            dst.flags |= 32u;
        }
        char extra[96];
        siblingFile(fileNameOf(tex.C_Str()), "Normal", extra, sizeof(extra));
        if (texOnDisk(texSearch, extra)) {
            dst.normalTexIndex = textureSlot(textures, extra);
        }
        aiString nrmMap;
        if (mat->GetTexture(aiTextureType_NORMALS, 0, &nrmMap) == AI_SUCCESS) {
            dst.normalTexIndex = textureSlot(textures, nrmMap.C_Str());
        }
        siblingFile(fileNameOf(tex.C_Str()), "Height", extra, sizeof(extra));
        if (texOnDisk(texSearch, extra)) {
            dst.pad[0] = textureSlot(textures, extra);
        }
        siblingFile(fileNameOf(tex.C_Str()), "Specular", extra, sizeof(extra));
        if (texOnDisk(texSearch, extra)) {
            dst.ormTexIndex = textureSlot(textures, extra);
            dst.flags |= 4u;
        }
        siblingFile(fileNameOf(tex.C_Str()), "Emissive", extra, sizeof(extra));
        if (texOnDisk(texSearch, extra)) {
            dst.emissiveTexIndex = textureSlot(textures, extra);
            dst.emissiveIntensity = 1.0f;
        }
        aiString ormName;
        if (mat->GetTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, &ormName) == AI_SUCCESS
            || mat->GetTexture(aiTextureType_METALNESS, 0, &ormName) == AI_SUCCESS
            || mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &ormName) == AI_SUCCESS
            || mat->GetTexture(aiTextureType_UNKNOWN, 0, &ormName) == AI_SUCCESS) {
            dst.ormTexIndex = textureSlot(textures, ormName.C_Str());
            dst.flags |= 4u;
        }
        const char* roughKinds[] = {"Roughness", "Rough"};
        for (const char* kind : roughKinds) {
            siblingFile(fileNameOf(tex.C_Str()), kind, extra, sizeof(extra));
            if (texOnDisk(texSearch, extra)) {
                dst.pad[2] = textureSlot(textures, extra);
                break;
            }
        }
        const char* metalKinds[] = {"Metallic", "Metalness", "Metal"};
        for (const char* kind : metalKinds) {
            siblingFile(fileNameOf(tex.C_Str()), kind, extra, sizeof(extra));
            if (texOnDisk(texSearch, extra)) {
                dst.pad[3] = textureSlot(textures, extra);
                break;
            }
        }
        if ((dst.flags & 4u) == 0u) {
            const char* ormKinds[] = {"MetallicRoughness", "ORM"};
            for (const char* kind : ormKinds) {
                siblingFile(fileNameOf(tex.C_Str()), kind, extra, sizeof(extra));
                if (texOnDisk(texSearch, extra)) {
                    dst.ormTexIndex = textureSlot(textures, extra);
                    dst.flags |= 4u;
                    break;
                }
            }
        }
        aiColor4D emit(0.0f, 0.0f, 0.0f, 1.0f);
        const bool hasEmit = mat->Get(AI_MATKEY_COLOR_EMISSIVE, emit) == AI_SUCCESS;
        const float emitPeak = std::max(emit.r, std::max(emit.g, emit.b));
        float strength = 0.0f;
        mat->Get(AI_MATKEY_EMISSIVE_INTENSITY, strength);
        aiString emTex;
        if (mat->GetTexture(aiTextureType_EMISSIVE, 0, &emTex) == AI_SUCCESS) {
            dst.emissiveTexIndex = textureSlot(textures, emTex.C_Str());
        }
        if (dst.emissiveTexIndex != 0 || (hasEmit && emitPeak > 0.001f)) {
            const float gain = strength > 0.0f ? strength : 1.0f;
            const float peak = emitPeak > 0.001f ? emitPeak : 1.0f;
            dst.emissiveIntensity = std::max(dst.emissiveIntensity, gain * peak);
        }
        float ior = 0.0f;
        if (mat->Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS && ior > 1.01f && ior < 3.0f) {
            const float fresnel = (ior - 1.0f) / (ior + 1.0f);
            const float perceptual = std::sqrt(std::max((fresnel * fresnel) / 0.16f, 0.0f));
            std::memcpy(&dst.pad[1], &perceptual, sizeof(float));
        }
        if (hand && dst.emissiveIntensity < 2.0f) {
            dst.emissiveIntensity = 2.5f;
        }
    }
    struct Placement {
        unsigned mesh = 0;
        aiMatrix4x4 world{};
        const char* name = "";
    };
    std::vector<Placement> placed;
    const auto collectNodes = [&](auto&& self, const aiNode* node, const aiMatrix4x4& parent) -> void {
        if (node == nullptr) {
            return;
        }
        const aiMatrix4x4 world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            Placement item{};
            item.mesh = node->mMeshes[i];
            item.world = world;
            item.name = node->mName.C_Str();
            placed.push_back(item);
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c) {
            self(self, node->mChildren[c], world);
        }
    };
    aiMatrix4x4 identity{};
    collectNodes(collectNodes, scene->mRootNode, identity);
    for (unsigned p = 0; p < placed.size(); ++p) {
        const Placement& place = placed[p];
        if (place.mesh >= scene->mNumMeshes) {
            continue;
        }
        const aiMesh* mesh = scene->mMeshes[place.mesh];
        if (mesh == nullptr || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
            continue;
        }
        std::vector<float> xyz(static_cast<size_t>(mesh->mNumVertices) * 3u);
        std::vector<float> nrm(static_cast<size_t>(mesh->mNumVertices) * 3u);
        std::vector<float> uv(static_cast<size_t>(mesh->mNumVertices) * 2u);
        std::vector<uint32_t> idx;
        idx.reserve(static_cast<size_t>(mesh->mNumFaces) * 3u);
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            bakePoint(place.world, mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z,
                xyz[static_cast<size_t>(v) * 3u + 0u], xyz[static_cast<size_t>(v) * 3u + 1u], xyz[static_cast<size_t>(v) * 3u + 2u]);
            if (mesh->mNormals != nullptr) {
                bakeNormal(place.world, mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z,
                    nrm[static_cast<size_t>(v) * 3u + 0u], nrm[static_cast<size_t>(v) * 3u + 1u], nrm[static_cast<size_t>(v) * 3u + 2u]);
            } else {
                nrm[static_cast<size_t>(v) * 3u + 1u] = 1.0f;
            }
            const uint32_t localMat = mesh->mMaterialIndex;
            const bool worldUv = localMat < static_cast<uint32_t>(materials.size() - materialBase)
                && (materials[materialBase + localMat].flags & 1u) != 0;
            const aiVector3D* uvSet = mesh->mTextureCoords[0];
            if (uvSet == nullptr) {
                uvSet = mesh->mTextureCoords[1];
            }
            if (!worldUv && uvSet != nullptr) {
                uv[static_cast<size_t>(v) * 2u + 0u] = uvSet[v].x;
                uv[static_cast<size_t>(v) * 2u + 1u] = 1.0f - uvSet[v].y;
            } else {
                uv[static_cast<size_t>(v) * 2u + 0u] = xyz[static_cast<size_t>(v) * 3u] * 0.25f;
                uv[static_cast<size_t>(v) * 2u + 1u] = xyz[static_cast<size_t>(v) * 3u + 2u] * 0.25f;
            }
        }
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) {
                continue;
            }
            idx.push_back(face.mIndices[0]);
            idx.push_back(face.mIndices[1]);
            idx.push_back(face.mIndices[2]);
        }
        const uint32_t material = mesh->mMaterialIndex < static_cast<uint32_t>(materials.size() - materialBase)
            ? materialBase + mesh->mMaterialIndex
            : materialBase;
        repairMesh(xyz, nrm, idx, matrixFlipsWinding(place.world));
        if (idx.empty()) {
            continue; 
        }
        const uint32_t group = groupBase + p;
        const size_t meshletBefore = build.meshlets.size();
        meshletBuildAppend(build, xyz.data(), nrm.data(), idx.data(), static_cast<uint32_t>(idx.size()), material, uv.data(), 0, group);
        if ((materials[material].flags & 2u) != 0u) {
            for (size_t s = meshletBefore; s < build.meshlets.size(); ++s) {
                build.meshlets[s].coneCutoff = -1.0f;
            }
        }
        std::fprintf(stderr, "scene import: %s %s  tris %zu  meshlets %zu\n",
            label, place.name, idx.size() / 3u, build.meshlets.size());
    }
    if (scene->mLights != nullptr) {
        for (unsigned i = 0; i < scene->mNumLights && lights.size() < kLightCap; ++i) {
            const aiLight* src = scene->mLights[i];
            if (src == nullptr) {
                continue;
            }
            std::vector<aiMatrix4x4> hits;
            if (scene->mRootNode != nullptr) {
                collectLightNodes(scene->mRootNode, aiMatrix4x4(), src->mName.C_Str(), false, hits);
                if (hits.empty()) {
                    collectLightNodes(scene->mRootNode, aiMatrix4x4(), src->mName.C_Str(), true, hits);
                }
            }
            if (hits.empty()) {
                pushLight(lights, src, src->mPosition, src->mDirection, 0.0f);
                continue;
            }
            const float crowd = hits.size() > 8 ? 2.5f : 0.0f;
            for (const aiMatrix4x4& world : hits) {
                pushLight(lights, src, world * src->mPosition, aiMatrix3x3(world) * src->mDirection, crowd);
            }
        }
        std::fprintf(stderr, "scene import: %s lights %zu\n", label, lights.size());
    }
    return true;
}

} // namespace

bool sceneImportFbxList(const char* const* fbxPaths, uint32_t count, const char* bhopPath) {
    if (fbxPaths == nullptr || bhopPath == nullptr || count == 0) {
        return false;
    }
    const unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals
        | aiProcess_JoinIdenticalVertices | aiProcess_FindInvalidData;
    std::vector<MaterialGpuData> materials;
    std::vector<SceneTexName> textures(1);
    std::vector<LightGpuData> lights;
    MeshletBuild build{};
    for (uint32_t file = 0; file < count; ++file) {
        if (fbxPaths[file] == nullptr) {
            continue;
        }
        Assimp::Importer importer;
        const char* slash = std::strrchr(fbxPaths[file], '/');
        const char* label = slash != nullptr ? slash + 1 : fbxPaths[file];
        std::fprintf(stderr, "read %s\n", label);
        const aiScene* scene = nullptr;
        try {
            scene = importer.ReadFile(fbxPaths[file], flags);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "assimp: %s\n", error.what());
            continue;
        }
        if (scene == nullptr || scene->mNumMeshes == 0) {
            std::fprintf(stderr, "assimp: %s\n", importer.GetErrorString());
            continue;
        }
        TexSearch texSearch{};
        texSearchFromFbx(fbxPaths[file], texSearch);
        if (!appendSceneMeshes(scene, label, texSearch, materials, textures, lights, build)) {
            return false;
        }
    }
    if (build.meshlets.empty()) {
        return false;
    }
    return sceneBlobWrite(
        bhopPath,
        build.meshlets.data(),
        static_cast<uint32_t>(build.meshlets.size()),
        build.vertices.data(),
        static_cast<uint32_t>(build.vertices.size()),
        build.indices.data(),
        static_cast<uint32_t>(build.indices.size()),
        build.instances.data(),
        static_cast<uint32_t>(build.instances.size()),
        materials.data(),
        static_cast<uint32_t>(materials.size()),
        textures.data(),
        static_cast<uint32_t>(textures.size()),
        lights.empty() ? nullptr : lights.data(),
        static_cast<uint32_t>(lights.size()));
}

bool sceneImportFbx(const char* fbxPath, const char* bhopPath) {
    const char* paths[] = {fbxPath};
    return sceneImportFbxList(paths, 1, bhopPath);
}

} // namespace burnhope
