#pragma once

#include "gfx/Meshlet.hpp"

#include <cmath>
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

// Процедурные примитивы в вершинах GpuVertex. UV лежат в pad0/pad1. Индексы локальные, от нуля.
// 24 вершин и 36 индексов куба с полуразмерами hx, hy, hz. Индексы локальные, от нуля.
inline void genBoxMesh(GpuVertex* vert, uint32_t* idx, float hx, float hy, float hz) {
    struct Face {
        float cx, cy, cz, ux, uy, uz, vx, vy, vz;
    };
    const Face faces[6] = {
        {0.0f, hy, 0.0f, hx, 0.0f, 0.0f, 0.0f, 0.0f, -hz},
        {0.0f, -hy, 0.0f, hx, 0.0f, 0.0f, 0.0f, 0.0f, hz},
        {hx, 0.0f, 0.0f, 0.0f, hy, 0.0f, 0.0f, 0.0f, hz},
        {-hx, 0.0f, 0.0f, 0.0f, hy, 0.0f, 0.0f, 0.0f, -hz},
        {0.0f, 0.0f, hz, hx, 0.0f, 0.0f, 0.0f, hy, 0.0f},
        {0.0f, 0.0f, -hz, hx, 0.0f, 0.0f, 0.0f, -hy, 0.0f},
    };
    const float corner[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    const uint32_t local[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    uint32_t vcount = 0;
    uint32_t icount = 0;
    for (const Face& face : faces) {
        float nx = face.uy * face.vz - face.uz * face.vy;
        float ny = face.uz * face.vx - face.ux * face.vz;
        float nz = face.ux * face.vy - face.uy * face.vx;
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen > 1.0e-6f) {
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;
        } else {
            ny = 1.0f;
        }
        const uint32_t faceBase = vcount;
        for (uint32_t c = 0; c < 4u; ++c) {
            GpuVertex& v = vert[vcount];
            v = {};
            v.px = face.cx + corner[c][0] * face.ux + corner[c][1] * face.vx;
            v.py = face.cy + corner[c][0] * face.uy + corner[c][1] * face.vy;
            v.pz = face.cz + corner[c][0] * face.uz + corner[c][1] * face.vz;
            v.pad0 = corner[c][0] * 0.5f + 0.5f;
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;
            v.pad1 = corner[c][1] * 0.5f + 0.5f;
            vcount += 1;
        }
        for (uint32_t t = 0; t < 6u; ++t) {
            idx[icount] = faceBase + local[t];
            icount += 1;
        }
    }
}

inline void genCubeMesh(GpuVertex* vert, uint32_t* idx, float half) {
    genBoxMesh(vert, idx, half, half, half);
}

inline void genPlaneMesh(GpuVertex* vert, uint32_t* idx) {
    genCubeMesh(vert, idx, 0.02f);
    for (uint32_t i = 0; i < 24u; ++i) {
        vert[i].py = 0.0f;
        vert[i].ny = 1.0f;
        vert[i].nx = 0.0f;
        vert[i].nz = 0.0f;
    }
}

inline void genSphereMesh(GpuVertex* vert, uint32_t* idx) {
    constexpr int kSlice = 8;
    constexpr int kStack = 4;
    uint32_t vcount = 0;
    for (int y = 0; y <= kStack; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(kStack);
        const float pitch = (v - 0.5f) * 3.14159265f;
        const float cy = std::cos(pitch);
        const float sy = std::sin(pitch);
        for (int x = 0; x <= kSlice; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(kSlice);
            const float yaw = u * 6.2831853f;
            GpuVertex& dst = vert[vcount++];
            dst = {};
            dst.px = std::cos(yaw) * cy * 0.5f;
            dst.py = sy * 0.5f;
            dst.pz = std::sin(yaw) * cy * 0.5f;
            dst.nx = dst.px * 2.0f;
            dst.ny = dst.py * 2.0f;
            dst.nz = dst.pz * 2.0f;
            dst.pad0 = u;
            dst.pad1 = v;
        }
    }
    uint32_t icount = 0;
    for (int y = 0; y < kStack; ++y) {
        for (int x = 0; x < kSlice; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * (kSlice + 1) + x);
            const uint32_t b = a + static_cast<uint32_t>(kSlice + 1);
            idx[icount++] = a;
            idx[icount++] = b;
            idx[icount++] = a + 1u;
            idx[icount++] = a + 1u;
            idx[icount++] = b;
            idx[icount++] = b + 1u;
        }
    }
}

inline void genCylinderMesh(GpuVertex* vert, uint32_t* idx) {
    constexpr int kSide = 8;
    uint32_t vcount = 0;
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? -0.5f : 0.5f;
        for (int i = 0; i < kSide; ++i) {
            const float ang = static_cast<float>(i) * 6.2831853f / static_cast<float>(kSide);
            GpuVertex& dst = vert[vcount++];
            dst = {};
            dst.px = std::cos(ang) * 0.5f;
            dst.py = y;
            dst.pz = std::sin(ang) * 0.5f;
            dst.nx = dst.px * 2.0f;
            dst.ny = 0.0f;
            dst.nz = dst.pz * 2.0f;
        }
    }
    GpuVertex& bottom = vert[vcount++];
    bottom = {};
    bottom.py = -0.5f;
    bottom.ny = -1.0f;
    GpuVertex& top = vert[vcount++];
    top = {};
    top.py = 0.5f;
    top.ny = 1.0f;
    uint32_t icount = 0;
    for (int i = 0; i < kSide; ++i) {
        const uint32_t n = static_cast<uint32_t>((i + 1) % kSide);
        idx[icount++] = static_cast<uint32_t>(i);
        idx[icount++] = n;
        idx[icount++] = static_cast<uint32_t>(kSide) + n;
        idx[icount++] = static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide) + n;
        idx[icount++] = static_cast<uint32_t>(kSide) + static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide * 2);
        idx[icount++] = n;
        idx[icount++] = static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide * 2 + 1);
        idx[icount++] = static_cast<uint32_t>(kSide) + static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide) + n;
    }
}


} // namespace burnhope
