#pragma once

#include "gfx/Material.hpp"
#include "gpu_scene/InstanceData.hpp"

#include <cstdint>

namespace burnhope {

inline uint32_t visPack(uint32_t instanceId, uint32_t primitiveId) {
    return ((instanceId & 0xFFFFFu) | ((primitiveId & 0xFFFu) << 20)) + 1u;
}

inline void visUnpack(uint32_t pixel, uint32_t& instanceId, uint32_t& primitiveId) {
    const uint32_t packed = pixel - 1u;
    instanceId = packed & 0xFFFFFu;
    primitiveId = packed >> 20;
}

struct VisHit {
    uint32_t instanceId = 0;
    uint32_t primitiveId = 0;
    uint32_t materialId = 0;
    MaterialGpuData material{};
};

inline bool visResolve(uint32_t pixel, const GpuInstance* instances, uint32_t instanceCount, const MaterialGpuData* materials, uint32_t materialCount, VisHit& out) {
    visUnpack(pixel, out.instanceId, out.primitiveId);
    if (instances == nullptr || out.instanceId >= instanceCount) {
        return false;
    }
    out.materialId = instances[out.instanceId].materialId;
    if (materials == nullptr || out.materialId >= materialCount) {
        return false;
    }
    out.material = materials[out.materialId];
    return true;
}

} // namespace burnhope
