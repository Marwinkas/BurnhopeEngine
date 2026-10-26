#pragma once

#include <cstdint>
#include <vector>

namespace burnhope {

constexpr uint32_t kModelMeshMaxTris = 300000;

// World-space triangle soup of a model: three floats per vertex, three indices per triangle.
struct ModelMesh {
    std::vector<float> xyz;
    std::vector<uint32_t> idx;
};

// Reads .gltf, .glb, .fbx, .obj (Assimp, node transforms baked in) or .bhop (LOD 0 meshlets). Models above
// kModelMeshMaxTris triangles are thinned evenly. False when the file cannot be read or holds no triangles.
[[nodiscard]] bool modelLoadMesh(const char* path, ModelMesh& out);

} // namespace burnhope
