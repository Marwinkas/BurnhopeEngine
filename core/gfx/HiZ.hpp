#pragma once

#include <cstdint>
#include <cmath>

namespace burnhope {

inline uint32_t hizMipForSphere(float radius, float viewZ, float screenHeight, float tanHalfFovY, uint32_t mipCount) {
    if (mipCount == 0 || radius <= 0.0f || viewZ <= 1.0e-4f || tanHalfFovY <= 1.0e-4f || screenHeight <= 1.0f) {
        return 0;
    }
    float pixels = (radius / viewZ) * screenHeight / tanHalfFovY;
    uint32_t mip = 0;
    while (pixels >= 2.0f && mip + 1 < mipCount) {
        pixels *= 0.5f;
        mip += 1;
    }
    return mip;
}

inline bool hizSphereVisible(const float* mip, uint32_t width, uint32_t height, float u, float v, float nearestDepth) {
    if (mip == nullptr || width == 0 || height == 0) {
        return true;
    }
    if (u < 0.0f) {
        u = 0.0f;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (u > 0.999f) {
        u = 0.999f;
    }
    if (v > 0.999f) {
        v = 0.999f;
    }
    const uint32_t x = static_cast<uint32_t>(u * static_cast<float>(width));
    const uint32_t y = static_cast<uint32_t>(v * static_cast<float>(height));
    const float farDepth = mip[y * width + x];
    return nearestDepth <= farDepth;
}

} // namespace burnhope
