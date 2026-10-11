#pragma once

#include <cmath>
#include <cstdint>

namespace burnhope {

// Same weights as shade, SSR and SSRC: pixel center against the projected triangle.
struct ScreenBary {
    float u = 0.0f;
    float v = 0.0f;
    float w = 0.0f;
    bool flat = false;
};

inline ScreenBary screenBary(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    const float v0x = bx - ax;
    const float v0y = by - ay;
    const float v1x = cx - ax;
    const float v1y = cy - ay;
    const float v2x = px - ax;
    const float v2y = py - ay;
    const float d00 = v0x * v0x + v0y * v0y;
    const float d01 = v0x * v1x + v0y * v1y;
    const float d11 = v1x * v1x + v1y * v1y;
    const float denom = d00 * d11 - d01 * d01;
    ScreenBary b{};
    if (std::fabs(denom) <= 1.0e-6f) {
        b.flat = true;
        b.u = 1.0f;
        return b;
    }
    b.v = (d11 * (v2x * v0x + v2y * v0y) - d01 * (v2x * v1x + v2y * v1y)) / denom;
    b.w = (d00 * (v2x * v1x + v2y * v1y) - d01 * (v2x * v0x + v2y * v0y)) / denom;
    b.u = 1.0f - b.v - b.w;
    return b;
}

} // namespace burnhope
