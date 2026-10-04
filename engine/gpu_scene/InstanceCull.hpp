#pragma once

#include "gpu_scene/Camera.hpp"
#include "gpu_scene/InstanceData.hpp"

#include <cstdint>

namespace burnhope {

struct InstanceBounds {
    float center[3]{};
    float radius = 0.0f;
};

// Sphere bounds. Same planes as the instance path. No allocation.
inline uint32_t cullInstances(const CameraCull& cam, const InstanceBounds* bounds, uint32_t count, uint32_t* outVisibleIndices, uint32_t visibleCap) {
    if (bounds == nullptr || outVisibleIndices == nullptr || visibleCap == 0) {
        return 0;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < visibleCap; ++i) {
        const InstanceBounds& b = bounds[i];
        if (cameraSphereVisible(cam, b.center[0], b.center[1], b.center[2], b.radius)) {
            outVisibleIndices[n++] = i;
        }
    }
    return n;
}

// Writes surviving source indices into `visible`. No allocation. Returns how many fit.
inline uint32_t cullInstances(
    const GpuInstance* src,
    uint32_t count,
    const CameraCull& cam,
    uint32_t* visible,
    uint32_t visibleCap) {
    if (src == nullptr || visible == nullptr || visibleCap == 0) {
        return 0;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (n >= visibleCap) {
            break;
        }
        const float* s = src[i].sphere;
        if (cameraSphereVisible(cam, s[0], s[1], s[2], s[3])) {
            visible[n++] = i;
        }
    }
    return n;
}

} // namespace burnhope
