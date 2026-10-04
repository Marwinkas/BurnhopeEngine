#include "asset/AssimpImporter.hpp"

#include "gfx/Light.hpp"
#include "gfx/MeshletBuilder.hpp"
#include "platform/Jobs.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
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

bool texOnDisk(const char* file) {
    if (file == nullptr || file[0] == '\0') {
        return false;
    }
    char path[320];
    std::snprintf(path, sizeof(path), "testscene/Textures/%s", file);
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    std::fclose(f);
    return true;
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

uint32_t textureSlot(std::vector<SceneTexName>& names, const char* path, char pack) {
    const char* file = fileNameOf(path);
    if (file[0] == '\0') {
        return 0;
    }
    char stored[64];
    if (pack != 0) {
        std::snprintf(stored, sizeof(stored), "%c/%s", pack, file);
    } else {
        std::snprintf(stored, sizeof(stored), "%s", file);
    }
    for (uint32_t i = 1; i < names.size(); ++i) {
        if (std::strcmp(names[i].file, stored) == 0) {
            return i;
        }
    }
    if (names.size() >= 768) {
        return 0;
    }
    SceneTexName name{};
    std::snprintf(name.file, sizeof(name.file), "%s", stored);
    names.push_back(name);
    return static_cast<uint32_t>(names.size() - 1);
}

void repairMesh(std::vector<float>& xyz, std::vector<float>& nrm, std::vector<uint32_t>& idx) {
    const uint32_t vertCount = static_cast<uint32_t>(xyz.size() / 3u);
    const uint32_t triCount = static_cast<uint32_t>(idx.size() / 3u);
    if (vertCount == 0 || triCount == 0) {
        idx.clear();
        return;
    }
    std::fill(nrm.begin(), nrm.end(), 0.0f);
    std::vector<uint32_t> kept;
    kept.reserve(idx.size());
    for (uint32_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = idx[static_cast<size_t>(t) * 3u];
        const uint32_t i1 = idx[static_cast<size_t>(t) * 3u + 1u];
        const uint32_t i2 = idx[static_cast<size_t>(t) * 3u + 2u];
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
        const float area = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (area < 1.0e-8f) {
            continue;
        }
        nrm[i0 * 3u] += nx;
        nrm[i0 * 3u + 1u] += ny;
        nrm[i0 * 3u + 2u] += nz;
        nrm[i1 * 3u] += nx;
        nrm[i1 * 3u + 1u] += ny;
        nrm[i1 * 3u + 2u] += nz;
        nrm[i2 * 3u] += nx;
        nrm[i2 * 3u + 1u] += ny;
        nrm[i2 * 3u + 2u] += nz;
        kept.push_back(i0);
        kept.push_back(i1);
        kept.push_back(i2);
    }
    for (uint32_t v = 0; v < vertCount; ++v) {
        const float x = nrm[v * 3u];
        const float y = nrm[v * 3u + 1u];
        const float z = nrm[v * 3u + 2u];
        const float len = std::sqrt(x * x + y * y + z * z);
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

constexpr uint32_t kSimTris = 50000;

struct SimChunk {
    const float* xyz = nullptr;
    size_t verts = 0;
    const uint32_t* src = nullptr;
    uint32_t tris = 0;
    bool sloppy = false;
    std::vector<uint32_t> kept;
};

void simOne(void* user) {
    auto* chunk = static_cast<SimChunk*>(user);
    chunk->kept.assign(static_cast<size_t>(chunk->tris) * 3u, 0u);
    float error = 0.0f;
    const size_t target = std::max<size_t>(3, (static_cast<size_t>(chunk->tris) * 3u) / 2u);
    size_t n = 0;
    if (chunk->sloppy) {
        n = meshopt_simplifySloppy(
            chunk->kept.data(), chunk->src, static_cast<size_t>(chunk->tris) * 3u,
            chunk->xyz, chunk->verts, sizeof(float) * 3,
            target, 0.02f, &error);
    } else {
        n = meshopt_simplify(
            chunk->kept.data(), chunk->src, static_cast<size_t>(chunk->tris) * 3u,
            chunk->xyz, chunk->verts, sizeof(float) * 3,
            target, 0.05f, meshopt_SimplifyLockBorder, &error);
    }
    chunk->kept.resize(n >= 3 ? n : 0);
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

bool appendBistro(
    const aiScene* scene,
    const char* label,
    std::vector<MaterialGpuData>& materials,
    std::vector<SceneTexName>& textures,
    std::vector<LightGpuData>& lights,
    MeshletBuild& build,
    char pack) {
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
        dst.albedoTexIndex = textureSlot(textures, tex.C_Str(), pack);
        aiTextureMapping mapping = aiTextureMapping_UV;
        mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex, &mapping);
        if (mapping == aiTextureMapping_BOX || mapping == aiTextureMapping_OTHER) {
            dst.flags |= 1u;
        }
        int twoSided = 0;
        if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS && twoSided != 0) {
            dst.flags |= 2u;
        }
        char extra[96];
        siblingFile(fileNameOf(tex.C_Str()), "Normal", extra, sizeof(extra));
        if (texOnDisk(extra)) {
            dst.normalTexIndex = textureSlot(textures, extra, pack);
        }
        aiString nrmMap;
        if (mat->GetTexture(aiTextureType_NORMALS, 0, &nrmMap) == AI_SUCCESS) {
            dst.normalTexIndex = textureSlot(textures, nrmMap.C_Str(), pack);
        }
        siblingFile(fileNameOf(tex.C_Str()), "Height", extra, sizeof(extra));
        if (texOnDisk(extra)) {
            dst.pad[0] = textureSlot(textures, extra, pack);
        }
        siblingFile(fileNameOf(tex.C_Str()), "Specular", extra, sizeof(extra));
        if (texOnDisk(extra)) {
            dst.ormTexIndex = textureSlot(textures, extra, pack);
        }
        aiString ormName;
        if (mat->GetTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, &ormName) == AI_SUCCESS
            || mat->GetTexture(aiTextureType_METALNESS, 0, &ormName) == AI_SUCCESS
            || mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &ormName) == AI_SUCCESS
            || mat->GetTexture(aiTextureType_UNKNOWN, 0, &ormName) == AI_SUCCESS) {
            dst.ormTexIndex = textureSlot(textures, ormName.C_Str(), pack);
            dst.flags |= 4u;
        }
    }
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (mesh == nullptr || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
            continue;
        }
        std::vector<float> xyz(static_cast<size_t>(mesh->mNumVertices) * 3u);
        std::vector<float> nrm(static_cast<size_t>(mesh->mNumVertices) * 3u);
        std::vector<float> uv(static_cast<size_t>(mesh->mNumVertices) * 2u);
        std::vector<uint32_t> idx;
        idx.reserve(static_cast<size_t>(mesh->mNumFaces) * 3u);
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            xyz[static_cast<size_t>(v) * 3u + 0u] = mesh->mVertices[v].x;
            xyz[static_cast<size_t>(v) * 3u + 1u] = mesh->mVertices[v].y;
            xyz[static_cast<size_t>(v) * 3u + 2u] = mesh->mVertices[v].z;
            if (mesh->mNormals != nullptr) {
                nrm[static_cast<size_t>(v) * 3u + 0u] = mesh->mNormals[v].x;
                nrm[static_cast<size_t>(v) * 3u + 1u] = mesh->mNormals[v].y;
                nrm[static_cast<size_t>(v) * 3u + 2u] = mesh->mNormals[v].z;
            } else {
                nrm[static_cast<size_t>(v) * 3u + 1u] = 1.0f;
            }
            const uint32_t localMat = mesh->mMaterialIndex;
            const bool worldUv = localMat < static_cast<uint32_t>(materials.size() - materialBase)
                && (materials[materialBase + localMat].flags & 1u) != 0;
            if (!worldUv && mesh->mTextureCoords[0] != nullptr) {
                uv[static_cast<size_t>(v) * 2u + 0u] = mesh->mTextureCoords[0][v].x;
                uv[static_cast<size_t>(v) * 2u + 1u] = mesh->mTextureCoords[0][v].y;
            } else {
                uv[static_cast<size_t>(v) * 2u + 0u] = mesh->mVertices[v].x * 0.25f;
                uv[static_cast<size_t>(v) * 2u + 1u] = mesh->mVertices[v].z * 0.25f;
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
        float minX = xyz[0];
        float maxX = xyz[0];
        float minY = xyz[1];
        float maxY = xyz[1];
        float minZ = xyz[2];
        float maxZ = xyz[2];
        for (size_t i = 0; i < xyz.size(); i += 3) {
            minX = std::min(minX, xyz[i]);
            maxX = std::max(maxX, xyz[i]);
            minY = std::min(minY, xyz[i + 1]);
            maxY = std::max(maxY, xyz[i + 1]);
            minZ = std::min(minZ, xyz[i + 2]);
            maxZ = std::max(maxZ, xyz[i + 2]);
        }
        repairMesh(xyz, nrm, idx);
        if (idx.empty()) {
            continue;
        }
        const bool sloppy = scene->mMaterials != nullptr && mesh->mMaterialIndex < scene->mNumMaterials
            && foliageMaterial(scene->mMaterials[mesh->mMaterialIndex]);
        const uint32_t lodBase = static_cast<uint32_t>(build.instances.size());
        meshletBuildAppend(build, xyz.data(), nrm.data(), idx.data(), static_cast<uint32_t>(idx.size()), material, uv.data(), 0, m);
        std::vector<uint32_t> level;
        level.swap(idx);
        uint32_t maxLod = 0;
        for (uint32_t lod = 1; lod <= 3 && level.size() >= 12; ++lod) {
            const uint32_t triCount = static_cast<uint32_t>(level.size() / 3);
            const uint32_t chunks = (triCount + kSimTris - 1u) / kSimTris;
            std::fprintf(stderr, "bistro %s mesh %u lod %u  tris %u  jobs %u  sloppy %d  threads %d\n",
                label, m + 1u, lod, triCount, chunks, sloppy ? 1 : 0, jobsWorkerCount());
            std::vector<SimChunk> parts(chunks);
            std::vector<Job> jobs(chunks);
            for (uint32_t c = 0; c < chunks; ++c) {
                const uint32_t t = c * kSimTris;
                parts[c].xyz = xyz.data();
                parts[c].verts = xyz.size() / 3u;
                parts[c].src = level.data() + static_cast<size_t>(t) * 3u;
                parts[c].tris = std::min(kSimTris, triCount - t);
                parts[c].sloppy = sloppy;
                jobs[c].fn = simOne;
                jobs[c].user = &parts[c];
            }
            jobsRunAndWait(jobs.data(), static_cast<int>(jobs.size()));
            std::vector<uint32_t> next;
            size_t total = 0;
            for (const SimChunk& part : parts) {
                total += part.kept.size();
            }
            next.reserve(total);
            for (SimChunk& part : parts) {
                next.insert(next.end(), part.kept.begin(), part.kept.end());
                std::vector<uint32_t>().swap(part.kept);
            }
            std::vector<SimChunk>().swap(parts);
            if (next.size() < 3 || next.size() >= level.size()) {
                break;
            }
            meshletBuildAppend(build, xyz.data(), nrm.data(), next.data(), static_cast<uint32_t>(next.size()), material, uv.data(), lod, m);
            maxLod = lod;
            level.swap(next);
            std::vector<uint32_t>().swap(next);
        }
        std::vector<float>().swap(xyz);
        std::vector<float>().swap(nrm);
        std::vector<float>().swap(uv);
        std::vector<uint32_t>().swap(level);
        if (maxLod > 0) {
            for (uint32_t i = lodBase; i < build.instances.size(); ++i) {
                const uint32_t group = build.instances[i].pad[1] & 0x0fffffffu;
                build.instances[i].pad[1] = group | 0x80000000u | (maxLod << 28);
            }
        }
        std::fprintf(stderr, "bistro %s mesh %u/%u  meshlets %zu\n", label, m + 1u, scene->mNumMeshes, build.meshlets.size());
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
        std::fprintf(stderr, "bistro %s lights %zu\n", label, lights.size());
    }
    return true;
}

} // namespace

bool sceneImportFbxList(const char* const* fbxPaths, uint32_t count, const char* bhopPath) {
    if (fbxPaths == nullptr || bhopPath == nullptr || count == 0) {
        return false;
    }
    const unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals
        | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices | aiProcess_FindDegenerates
        | aiProcess_FindInvalidData;
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
        char pack = 0;
        if (std::strstr(fbxPaths[file], "main_sponza") != nullptr) {
            pack = 'a';
        } else if (std::strstr(fbxPaths[file], "pkg_a_curtains") != nullptr) {
            pack = 'b';
        } else if (std::strstr(fbxPaths[file], "pkg_b_ivy") != nullptr) {
            pack = 'c';
        } else if (std::strstr(fbxPaths[file], "pkg_c_trees") != nullptr) {
            pack = 'd';
        } else if (std::strstr(fbxPaths[file], "pkg_d_10k_candles") != nullptr) {
            pack = 'e';
        }
        if (!appendBistro(scene, label, materials, textures, lights, build, pack)) {
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
