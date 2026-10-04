#pragma once

#include "gfx/HiZ.hpp"
#include "gfx/Meshlet.hpp"
#include "gpu_scene/Camera.hpp"

#include <cmath>
#include <cstdint>

namespace burnhope {

inline bool meshletPassCull(const CameraCull& cam, const Meshlet& meshlet, float screenHeight, uint32_t mipCount, uint32_t& outMip) {
    outMip = 0;
    if (!cameraSphereVisible(cam, meshlet.center[0], meshlet.center[1], meshlet.center[2], meshlet.radius)) {
        return false;
    }
    if (!cameraConeVisible(cam, meshlet.center[0], meshlet.center[1], meshlet.center[2], meshlet.coneAxis[0], meshlet.coneAxis[1], meshlet.coneAxis[2], meshlet.coneCutoff)) {
        return false;
    }
    const float dx = cam.pos[0] - meshlet.center[0];
    const float dy = cam.pos[1] - meshlet.center[1];
    const float dz = cam.pos[2] - meshlet.center[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    outMip = hizMipForSphere(meshlet.radius, dist, screenHeight, cam.tanHalfFovY, mipCount);
    return true;
}

} // namespace burnhope
