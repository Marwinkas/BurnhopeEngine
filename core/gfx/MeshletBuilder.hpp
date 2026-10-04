#pragma once

#include "asset/SceneBlob.hpp"
#include "gfx/PrimitiveGen.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace burnhope {

struct MeshletBuild {
    std::vector<Meshlet> meshlets;
    std::vector<SceneVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SceneInstance> instances;
};

inline void meshletBuildAppend(
    MeshletBuild& build,
    const float* xyz,
    const float* nrm,
    const uint32_t* idx,
    uint32_t indexCount,
    uint32_t materialId,
    const float* uv = nullptr,
    uint32_t lod = 0,
    uint32_t group = 0) {
    if (xyz == nullptr || idx == nullptr || indexCount < 3) {
        return;
    }
    struct Cursor {
        uint32_t src[kMeshletMaxVerts]{};
        float pos[kMeshletMaxVerts][3]{};
        float nrm[kMeshletMaxVerts][3]{};
        uint32_t count = 0;
        uint32_t tris = 0;
        uint32_t vertBase = 0;
        uint32_t indexBase = 0;
    };
    Cursor cursor{};
    const auto find = [](const Cursor& c, uint32_t src) -> int {
        for (uint32_t i = 0; i < c.count; ++i) {
            if (c.src[i] == src) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const auto flush = [&]() {
        if (cursor.tris == 0) {
            return;
        }
        Meshlet meshlet{};
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;
        for (uint32_t i = 0; i < cursor.count; ++i) {
            cx += cursor.pos[i][0];
            cy += cursor.pos[i][1];
            cz += cursor.pos[i][2];
            ax += cursor.nrm[i][0];
            ay += cursor.nrm[i][1];
            az += cursor.nrm[i][2];
        }
        const float inv = 1.0f / static_cast<float>(cursor.count);
        cx *= inv;
        cy *= inv;
        cz *= inv;
        float radius = 0.0f;
        for (uint32_t i = 0; i < cursor.count; ++i) {
            const float dx = cursor.pos[i][0] - cx;
            const float dy = cursor.pos[i][1] - cy;
            const float dz = cursor.pos[i][2] - cz;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > radius) {
                radius = d;
            }
        }
        const float al = std::sqrt(ax * ax + ay * ay + az * az);
        if (al > 1.0e-8f) {
            ax /= al;
            ay /= al;
            az /= al;
        } else {
            ax = 0.0f;
            ay = 1.0f;
            az = 0.0f;
        }
        float cutoff = 1.0f;
        for (uint32_t i = 0; i < cursor.count; ++i) {
            const float nl = std::sqrt(cursor.nrm[i][0] * cursor.nrm[i][0] + cursor.nrm[i][1] * cursor.nrm[i][1] + cursor.nrm[i][2] * cursor.nrm[i][2]);
            if (nl < 1.0e-8f) {
                continue;
            }
            const float dot = (cursor.nrm[i][0] / nl) * ax + (cursor.nrm[i][1] / nl) * ay + (cursor.nrm[i][2] / nl) * az;
            if (dot < cutoff) {
                cutoff = dot;
            }
        }
        meshlet.center[0] = cx;
        meshlet.center[1] = cy;
        meshlet.center[2] = cz;
        meshlet.radius = radius;
        meshlet.coneAxis[0] = ax;
        meshlet.coneAxis[1] = ay;
        meshlet.coneAxis[2] = az;
        meshlet.coneCutoff = cutoff;
        meshlet.vertexOffset = cursor.vertBase;
        meshlet.triangleOffset = cursor.indexBase / 3u;
        meshlet.vertexCount = static_cast<uint8_t>(cursor.count);
        meshlet.triangleCount = static_cast<uint8_t>(cursor.tris);
        const uint32_t meshId = static_cast<uint32_t>(build.meshlets.size());
        build.meshlets.push_back(meshlet);
        SceneInstance instance{};
        instance.world[0] = instance.world[5] = instance.world[10] = 1.0f;
        instance.sphere[0] = cx;
        instance.sphere[1] = cy;
        instance.sphere[2] = cz;
        instance.sphere[3] = radius;
        instance.materialId = materialId;
        instance.meshId = meshId;
        instance.pad[0] = lod;
        instance.pad[1] = group;
        build.instances.push_back(instance);
        cursor = {};
        cursor.vertBase = static_cast<uint32_t>(build.vertices.size());
        cursor.indexBase = static_cast<uint32_t>(build.indices.size());
    };
    const auto pushVert = [&](uint32_t src) -> uint32_t {
        const int found = find(cursor, src);
        if (found >= 0) {
            return static_cast<uint32_t>(found);
        }
        const uint32_t local = cursor.count;
        cursor.src[local] = src;
        cursor.pos[local][0] = xyz[src * 3 + 0];
        cursor.pos[local][1] = xyz[src * 3 + 1];
        cursor.pos[local][2] = xyz[src * 3 + 2];
        cursor.nrm[local][0] = nrm != nullptr ? nrm[src * 3 + 0] : 0.0f;
        cursor.nrm[local][1] = nrm != nullptr ? nrm[src * 3 + 1] : 1.0f;
        cursor.nrm[local][2] = nrm != nullptr ? nrm[src * 3 + 2] : 0.0f;
        SceneVertex vertex{};
        vertex.px = cursor.pos[local][0];
        vertex.py = cursor.pos[local][1];
        vertex.pz = cursor.pos[local][2];
        vertex.nx = cursor.nrm[local][0];
        vertex.ny = cursor.nrm[local][1];
        vertex.nz = cursor.nrm[local][2];
        if (uv != nullptr) {
            vertex.u = uv[src * 2 + 0];
            vertex.v = uv[src * 2 + 1];
        }
        build.vertices.push_back(vertex);
        cursor.count += 1;
        return local;
    };
    for (uint32_t t = 0; t + 2 < indexCount; t += 3) {
        const uint32_t i0 = idx[t];
        const uint32_t i1 = idx[t + 1];
        const uint32_t i2 = idx[t + 2];
        uint32_t extra = 0;
        if (find(cursor, i0) < 0) {
            extra += 1;
        }
        if (i1 != i0 && find(cursor, i1) < 0) {
            extra += 1;
        }
        if (i2 != i0 && i2 != i1 && find(cursor, i2) < 0) {
            extra += 1;
        }
        if (cursor.tris == kMeshletMaxTris || cursor.count + extra > kMeshletMaxVerts) {
            flush();
        }
        const uint32_t l0 = pushVert(i0);
        const uint32_t l1 = pushVert(i1);
        const uint32_t l2 = pushVert(i2);
        build.indices.push_back(l0);
        build.indices.push_back(l1);
        build.indices.push_back(l2);
        cursor.tris += 1;
    }
    flush();
}

} // namespace burnhope
