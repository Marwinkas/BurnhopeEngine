#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kDecalCap = 64;

// Oriented box. axis* is the full half-extent vector, not a unit axis.
struct alignas(16) DecalGpu {
    float center[3]{};
    float radius = 0.0f;
    float axisX[3]{};
    float opacity = 1.0f;
    float axisY[3]{};
    uint32_t texture = 0;
    float axisZ[3]{};
    float pad = 0.0f;
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

static_assert(std::is_trivially_copyable_v<DecalGpu>);
static_assert(sizeof(DecalGpu) == 80);

inline bool decalContains(const DecalGpu& decal, float x, float y, float z) {
    const float dx = x - decal.center[0];
    const float dy = y - decal.center[1];
    const float dz = z - decal.center[2];
    const float xx = decal.axisX[0] * decal.axisX[0] + decal.axisX[1] * decal.axisX[1] + decal.axisX[2] * decal.axisX[2];
    const float yy = decal.axisY[0] * decal.axisY[0] + decal.axisY[1] * decal.axisY[1] + decal.axisY[2] * decal.axisY[2];
    const float zz = decal.axisZ[0] * decal.axisZ[0] + decal.axisZ[1] * decal.axisZ[1] + decal.axisZ[2] * decal.axisZ[2];
    if (xx < 1.0e-8f || yy < 1.0e-8f || zz < 1.0e-8f) {
        return false;
    }
    const float lx = (dx * decal.axisX[0] + dy * decal.axisX[1] + dz * decal.axisX[2]) / xx;
    const float ly = (dx * decal.axisY[0] + dy * decal.axisY[1] + dz * decal.axisY[2]) / yy;
    const float lz = (dx * decal.axisZ[0] + dy * decal.axisZ[1] + dz * decal.axisZ[2]) / zz;
    return lx > -1.0f && lx < 1.0f && ly > -1.0f && ly < 1.0f && lz > -1.0f && lz < 1.0f;
}

} // namespace burnhope
