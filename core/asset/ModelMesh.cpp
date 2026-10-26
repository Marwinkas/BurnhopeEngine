#include "asset/ModelMesh.hpp"

#include "Str.hpp"
#include "asset/SceneBlob.hpp"
#include "io/File.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <exception>

namespace burnhope {
namespace {

bool loadBhop(const char* path, ModelMesh& out) {
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        fileUnmap(&file);
        return false;
    }
    SceneBlobView view{};
    const bool valid = sceneBlobView(file.data, file.size, &view);
    if (valid) {
        uint64_t total = 0;
        for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
            const SceneInstance& inst = view.instances[i];
            if (inst.pad[0] == 0 && inst.meshId < view.info->meshletCount) {
                total += view.meshlets[inst.meshId].triangleCount;
            }
        }
        const uint32_t stride = static_cast<uint32_t>(std::max<uint64_t>(1, (total + kModelMeshMaxTris - 1) / kModelMeshMaxTris));
        uint64_t seen = 0;
        for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
            const SceneInstance& inst = view.instances[i];
            if (inst.pad[0] != 0 || inst.meshId >= view.info->meshletCount) {
                continue;
            }
            const Meshlet& m = view.meshlets[inst.meshId];
            for (uint32_t t = 0; t < m.triangleCount; ++t, ++seen) {
                if (seen % stride != 0) {
                    continue;
                }
                for (uint32_t k = 0; k < 3; ++k) {
                    const uint32_t local = view.indices[(m.triangleOffset + t) * 3u + k];
                    const SceneVertex& v = view.vertices[m.vertexOffset + local];
                    const float* w = inst.world;
                    out.idx.push_back(static_cast<uint32_t>(out.xyz.size() / 3));
                    out.xyz.push_back(w[0] * v.px + w[1] * v.py + w[2] * v.pz + w[3]);
                    out.xyz.push_back(w[4] * v.px + w[5] * v.py + w[6] * v.pz + w[7]);
                    out.xyz.push_back(w[8] * v.px + w[9] * v.py + w[10] * v.pz + w[11]);
                }
            }
        }
    }
    fileUnmap(&file);
    return valid && !out.idx.empty();
}

bool loadAssimp(const char* path, ModelMesh& out) {
    Assimp::Importer importer;
    const aiScene* scene = nullptr;
    try {
        scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_FindInvalidData);
    } catch (const std::exception&) {
        return false;
    }
    if (scene == nullptr || scene->mNumMeshes == 0) {
        return false;
    }
    uint64_t total = 0;
    for (uint32_t m = 0; m < scene->mNumMeshes; ++m) {
        total += scene->mMeshes[m]->mNumFaces;
    }
    const uint32_t stride = static_cast<uint32_t>(std::max<uint64_t>(1, (total + kModelMeshMaxTris - 1) / kModelMeshMaxTris));
    uint64_t seen = 0;
    for (uint32_t m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (uint32_t f = 0; f < mesh->mNumFaces; ++f, ++seen) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3 || seen % stride != 0) {
                continue;
            }
            for (uint32_t k = 0; k < 3; ++k) {
                const aiVector3D& p = mesh->mVertices[face.mIndices[k]];
                out.idx.push_back(static_cast<uint32_t>(out.xyz.size() / 3));
                out.xyz.push_back(p.x);
                out.xyz.push_back(p.y);
                out.xyz.push_back(p.z);
            }
        }
    }
    return !out.idx.empty();
}

} // namespace

bool modelLoadMesh(const char* path, ModelMesh& out) {
    out.xyz.clear();
    out.idx.clear();
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    return strExtIs(path, ".bhop") ? loadBhop(path, out) : loadAssimp(path, out);
}

} // namespace burnhope
