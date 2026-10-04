#pragma once

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

// What the visbuffer mesh shader reads. Addresses are VkDeviceAddress. 24 bytes.
struct VisInstancePush {
    uint64_t instances = 0;
    uint64_t visible = 0;
    uint32_t visibleCount = 0;
    uint32_t pad = 0;
};

static_assert(std::is_trivially_copyable_v<VisInstancePush>);
static_assert(sizeof(VisInstancePush) == 24);

} // namespace burnhope
