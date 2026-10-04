#pragma once

#include <cmath>
#include <cstdint>

namespace burnhope {

inline float hammonDiffuse(float nDotL, float nDotV, float vDotL, float roughness) {
    const float nl = std::fmin(std::fmax(nDotL, 0.0f), 1.0f);
    const float nv = std::fmin(std::fmax(nDotV, 0.0f), 1.0f);
    const float facing = 0.5f + 0.5f * vDotL;
    const float rough = std::fmin(std::fmax(roughness, 0.0f), 1.0f);
    const float single = (1.05f / 3.14159265f) * (1.0f - std::pow(1.0f - nl, 5.0f)) * (1.0f - std::pow(1.0f - nv, 5.0f));
    const float retro = rough * rough * facing / 3.14159265f;
    return single * (1.0f - rough) + retro;
}

inline float coatLeft(float nDotV, float coat) {
    const float nv = std::fmin(std::fmax(nDotV, 0.0f), 1.0f);
    const float fresnel = std::pow(1.0f - nv, 5.0f);
    const float f = 0.04f + (1.0f - 0.04f) * fresnel;
    const float c = std::fmin(std::fmax(coat, 0.0f), 1.0f);
    return 1.0f - f * c;
}

inline float pcssPenumbra(float receiver, float blocker, float disk) {
    if (blocker <= 1.0e-4f || receiver <= blocker) {
        return 1.0f;
    }
    const float width = (receiver - blocker) / blocker * std::fmax(disk, 0.2f);
    return std::fmin(std::fmax(width * 8.0f, 1.0f), 12.0f);
}

inline void vogelDisk(int index, int count, float turn, float& x, float& y) {
    const float n = static_cast<float>(count < 1 ? 1 : count);
    const float r = std::sqrt((static_cast<float>(index) + 0.5f) / n);
    const float theta = 2.39996323f * static_cast<float>(index) + turn;
    x = std::cos(theta) * r;
    y = std::sin(theta) * r;
}

inline float blueNoise64(uint32_t x, uint32_t y) {
    uint32_t n = (x & 63u) + (y & 63u) * 64u;
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return static_cast<float>(n & 0x7fffffffu) / 2147483647.0f;
}

inline float sscsHandoff(float along) {
    if (along <= 0.05f) {
        return 0.0f;
    }
    if (along >= 0.20f) {
        return 1.0f;
    }
    return (along - 0.05f) / 0.15f;
}

inline float casDelta(float center, float neighbor, float sharp) {
    return (center - neighbor) * sharp;
}

inline float froxelSlice(float zn, float zf, uint32_t index, uint32_t count) {
    const float p = static_cast<float>(index) / static_cast<float>(count == 0u ? 1u : count);
    return zn * std::pow(zf / zn, p);
}

inline float ggxCone(float roughness) {
    const float r = std::fmin(std::fmax(roughness, 0.0f), 1.0f);
    return r * r * 0.35f;
}

inline void planarReflect(float vx, float vy, float vz, float& ox, float& oy, float& oz) {
    ox = -vx;
    oy = vy;
    oz = -vz;
}

inline float ltcForm(float cosTheta) {
    const float c = std::fmin(std::fmax(cosTheta, 0.0f), 1.0f);
    return c * c * 0.5f;
}

inline uint32_t cloudNoiseIndex(uint32_t x, uint32_t y, uint32_t z, uint32_t res, uint32_t base) {
    return base + x + y * res + z * res * res;
}

inline uint32_t outdoorProbe(uint32_t n, uint32_t x, uint32_t z) {
    const uint32_t grid = n == 0u ? 1u : n;
    return x + (grid - 1u) * grid + z * grid * grid;
}

inline uint32_t shadowPage(uint32_t x, uint32_t y, uint32_t page) {
    return ((y / page) * (4096u / page) + (x / page));
}

inline uint32_t shadowPagesCrossed(int x0, int y0, int x1, int y1) {
    if (x0 == x1 && y0 == y1) {
        return 0u;
    }
    const int dx = (x1 > x0 ? x1 - x0 : x0 - x1) + 1;
    const int dy = (y1 > y0 ? y1 - y0 : y0 - y1) + 1;
    return static_cast<uint32_t>(dx * dy);
}

inline uint32_t spotPage(float screenPx) {
    return screenPx < 96.0f ? 64u : 512u;
}

inline uint32_t dominantFace(float x, float y, float z) {
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    const float az = std::fabs(z);
    if (ax >= ay && ax >= az) {
        return x >= 0.0f ? 0u : 1u;
    }
    if (ay >= az) {
        return y >= 0.0f ? 2u : 3u;
    }
    return z >= 0.0f ? 4u : 5u;
}

inline float ltcCorner(float a, float b, float c, float d) {
    return (a + b + c + d) * 0.25f;
}

} // namespace burnhope
