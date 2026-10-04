#pragma once

#include "gfx/Light.hpp"
#include "gpu_scene/Camera.hpp"

#include <cmath>
#include <cstdint>

namespace burnhope {

constexpr uint32_t kPointShadowSize = 512;

inline float pointShadowDepth(float x, float y, float z, const LightGpuData& light) {
    const float dx = x - light.pos[0];
    const float dy = y - light.pos[1];
    const float dz = z - light.pos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (light.radius <= 1.0e-6f) {
        return 1.0f;
    }
    return dist / light.radius;
}

inline void pointShadowAssign(LightGpuData* lights, uint32_t count) {
    if (lights == nullptr) {
        return;
    }
    int32_t next = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (lights[i].type != kLightPoint || lights[i].radius <= 0.0f) {
            lights[i].shadowMapIndex = -1;
            continue;
        }
        lights[i].shadowMapIndex = next;
        next += 1;
    }
}

inline Mat4 pointShadowLook(float ex, float ey, float ez, float fx, float fy, float fz, float ux, float uy, float uz) {
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl;
    fy /= fl;
    fz /= fl;
    float sx = uy * fz - uz * fy;
    float sy = uz * fx - ux * fz;
    float sz = ux * fy - uy * fx;
    const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl;
    sy /= sl;
    sz /= sl;
    const float vx = sy * fz - sz * fy;
    const float vy = sz * fx - sx * fz;
    const float vz = sx * fy - sy * fx;
    Mat4 r = mat4Identity();
    r.m[0] = sx;
    r.m[1] = vx;
    r.m[2] = -fx;
    r.m[4] = sy;
    r.m[5] = vy;
    r.m[6] = -fy;
    r.m[8] = sz;
    r.m[9] = vz;
    r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(vx * ex + vy * ey + vz * ez);
    r.m[14] = fx * ex + fy * ey + fz * ez;
    return r;
}

inline Mat4 pointShadowFace(const LightGpuData& light, uint32_t face) {
    const float dir[6][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    };
    const float up[6][3] = {
        {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
    };
    const uint32_t f = face < 6 ? face : 0;
    const float farZ = light.radius > 0.05f ? light.radius : 0.05f;
    const Mat4 view = pointShadowLook(
        light.pos[0], light.pos[1], light.pos[2],
        dir[f][0], dir[f][1], dir[f][2],
        up[f][0], up[f][1], up[f][2]);
    const Mat4 proj = mat4PerspectiveYUpRh(1.5707963f, 1.0f, 0.05f, farZ);
    return mat4Mul(proj, view);
}

} // namespace burnhope
