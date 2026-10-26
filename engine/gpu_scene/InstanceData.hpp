#pragma once

#include "gfx/Meshlet.hpp"
#include "gpu_scene/Mat4.hpp"

#include <cstdint>
#include <type_traits>

namespace burnhope {

// One drawable. Affine world is 3 rows × float4 (xyz + translation). 80 bytes, 16-aligned.
struct GpuInstance {
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

static_assert(std::is_trivially_copyable_v<GpuInstance>);
static_assert(sizeof(GpuInstance) == 80);
static_assert(sizeof(GpuInstance) % 16 == 0);

// Точка модели в мире по аффинной world.
inline void instanceTransformPoint(const GpuInstance& inst, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = inst.world[0] * x + inst.world[1] * y + inst.world[2] * z + inst.world[3];
    oy = inst.world[4] * x + inst.world[5] * y + inst.world[6] * z + inst.world[7];
    oz = inst.world[8] * x + inst.world[9] * y + inst.world[10] * z + inst.world[11];
}

// Треугольник модели в мире. normal = (p1 - p0) × (p2 - p0) без нормировки, area2 = |normal|².
inline void instanceTransformTriangle(
    const GpuInstance& inst, const GpuVertex& a, const GpuVertex& b, const GpuVertex& c,
    float p0[3], float p1[3], float p2[3], float normal[3], float& area2) {
    instanceTransformPoint(inst, a.px, a.py, a.pz, p0[0], p0[1], p0[2]);
    instanceTransformPoint(inst, b.px, b.py, b.pz, p1[0], p1[1], p1[2]);
    instanceTransformPoint(inst, c.px, c.py, c.pz, p2[0], p2[1], p2[2]);
    float e1[3];
    float e2[3];
    vec3Sub(p1, p0, e1);
    vec3Sub(p2, p0, e2);
    vec3Cross(e1, e2, normal);
    area2 = vec3Dot(normal, normal);
}

inline void instanceSet(GpuInstance& g, float x, float y, float z, float radius, uint32_t material, uint32_t mesh) {
    g = {};
    g.world[0] = g.world[5] = g.world[10] = 1.0f;
    g.world[3] = x;
    g.world[7] = y;
    g.world[11] = z;
    g.sphere[0] = x;
    g.sphere[1] = y;
    g.sphere[2] = z;
    g.sphere[3] = radius;
    g.materialId = material;
    g.meshId = mesh;
}

} // namespace burnhope
