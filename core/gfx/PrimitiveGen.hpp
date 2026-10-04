#pragma once

#include "gfx/Meshlet.hpp"

#include <cstdint>
#include <cstring>

namespace burnhope {

constexpr uint32_t kMeshletMaxVerts = 64;
constexpr uint32_t kMeshletMaxTris = 124;

struct MeshletBlob {
    Meshlet meshlets[4]{};
    uint32_t meshletCount = 0;
    MeshletVertex vertices[kMeshletMaxVerts]{};
    uint32_t vertexCount = 0;
    uint8_t indices[kMeshletMaxTris * 3]{};
    uint32_t triangleCount = 0;
    uint32_t materialId = 0;
};

inline void meshletPushTri(MeshletBlob& blob, uint8_t a, uint8_t b, uint8_t c) {
    if (blob.triangleCount >= kMeshletMaxTris) {
        return;
    }
    const uint32_t at = blob.triangleCount * 3;
    blob.indices[at] = a;
    blob.indices[at + 1] = b;
    blob.indices[at + 2] = c;
    blob.triangleCount += 1;
}

inline void meshletFinish(MeshletBlob& blob) {
    blob.meshletCount = 1;
    blob.meshlets[0] = {};
    meshletFit(blob.meshlets[0], blob.vertices, blob.vertexCount);
    blob.meshlets[0].vertexCount = static_cast<uint8_t>(blob.vertexCount);
    blob.meshlets[0].triangleCount = static_cast<uint8_t>(blob.triangleCount > 255 ? 255 : blob.triangleCount);
}

inline void genPlaneMeshlets(float size, uint32_t materialId, MeshletBlob* out) {
    if (out == nullptr) {
        return;
    }
    *out = {};
    out->materialId = materialId;
    const float h = size * 0.5f;
    const float pts[4][3] = {{-h, 0.0f, -h}, {h, 0.0f, -h}, {h, 0.0f, h}, {-h, 0.0f, h}};
    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (uint32_t i = 0; i < 4; ++i) {
        MeshletVertex& v = out->vertices[i];
        v.pos[0] = pts[i][0];
        v.pos[1] = pts[i][1];
        v.pos[2] = pts[i][2];
        v.uv[0] = uvs[i][0];
        v.uv[1] = uvs[i][1];
        v.packedNormal = meshletPackNormal(0.0f, 1.0f, 0.0f);
    }
    out->vertexCount = 4;
    meshletPushTri(*out, 0, 1, 2);
    meshletPushTri(*out, 2, 3, 0);
    meshletFinish(*out);
    out->meshlets[0].coneAxis[0] = 0.0f;
    out->meshlets[0].coneAxis[1] = 1.0f;
    out->meshlets[0].coneAxis[2] = 0.0f;
    out->meshlets[0].coneCutoff = 0.5f;
}

inline void genCubeMeshlets(float size, uint32_t materialId, MeshletBlob* out) {
    if (out == nullptr) {
        return;
    }
    *out = {};
    out->materialId = materialId;
    const float h = size * 0.5f;
    const float p[8][3] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h},
    };
    for (uint32_t i = 0; i < 8; ++i) {
        MeshletVertex& v = out->vertices[i];
        v.pos[0] = p[i][0];
        v.pos[1] = p[i][1];
        v.pos[2] = p[i][2];
        v.uv[0] = (i & 1) != 0 ? 1.0f : 0.0f;
        v.uv[1] = (i & 2) != 0 ? 1.0f : 0.0f;
        v.packedNormal = meshletPackNormal(p[i][0], p[i][1], p[i][2]);
    }
    out->vertexCount = 8;
    const uint8_t faces[12][3] = {
        {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4}, {3, 6, 2}, {3, 7, 6},
        {0, 7, 3}, {0, 4, 7}, {1, 2, 6}, {1, 6, 5},
    };
    for (const uint8_t* face : faces) {
        meshletPushTri(*out, face[0], face[1], face[2]);
    }
    meshletFinish(*out);
}

} // namespace burnhope
