#pragma once

#include <algorithm>
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
    const float cu = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width) - 0.5f;
    const float cv = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height) - 0.5f;
    const uint32_t x0 = static_cast<uint32_t>(std::clamp(static_cast<int>(std::floor(cu)), 0, static_cast<int>(width - 1)));
    const uint32_t y0 = static_cast<uint32_t>(std::clamp(static_cast<int>(std::floor(cv)), 0, static_cast<int>(height - 1)));
    const uint32_t x1 = std::min(x0 + 1u, width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, height - 1u);
    const float d00 = mip[y0 * width + x0];
    const float d10 = mip[y0 * width + x1];
    const float d01 = mip[y1 * width + x0];
    const float d11 = mip[y1 * width + x1];
    const float farDepth = std::max(std::max(d00, d10), std::max(d01, d11));
    return nearestDepth <= farDepth;
}

} // namespace burnhope
