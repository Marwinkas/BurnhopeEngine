#pragma once

#include "gpu_scene/Mat4.hpp"
#include "gpu_scene/Camera.hpp"

#include <cmath>
#include <cstdint>

namespace burnhope {

constexpr uint32_t kSunShadowResolution = 2048;

struct SunShadow {
    Mat4 view{};
    Mat4 proj{};
    Mat4 viewProj{};
    uint32_t resolution = kSunShadowResolution;
    float radius = 0.0f;
    uint32_t pad[2]{};
};

inline Mat4 mat4OrthoRh(float left, float right, float bottom, float top, float zn, float zf) {
    Mat4 r{};
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (bottom - top);
    r.m[10] = 1.0f / (zn - zf);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (bottom - top);
    r.m[14] = zn / (zn - zf);
    r.m[15] = 1.0f;
    return r;
}

inline SunShadow sunShadowBuildAt(float cx, float cy, float cz, float dirX, float dirY, float dirZ, float extent = 20.0f, float snapCells = 1.0f) {
    SunShadow sun{};
    const float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    const float s = len > 1.0e-6f ? 1.0f / len : 1.0f;
    dirX *= s;
    dirY *= s;
    dirZ *= s;
    float fx = dirX;
    float fy = dirY;
    float fz = dirZ;
    float upx = 0.0f;
    float upy = 1.0f;
    float upz = 0.0f;
    if (std::fabs(fy) > 0.999f) {
        upx = 0.0f;
        upy = 0.0f;
        upz = fy > 0.0f ? -1.0f : 1.0f;
    }
    float rx = fy * upz - fz * upy;
    float ry = fz * upx - fx * upz;
    float rz = fx * upy - fy * upx;
    const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    const float rs = rl > 1.0e-6f ? 1.0f / rl : 1.0f;
    rx *= rs;
    ry *= rs;
    rz *= rs;
    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;
    const float cells = snapCells > 1.0f ? snapCells : 1.0f;
    const float texel = (2.0f * extent) / static_cast<float>(kSunShadowResolution) * cells;
    const float su = std::round((cx * rx + cy * ry + cz * rz) / texel) * texel;
    const float sv = std::round((cx * ux + cy * uy + cz * uz) / texel) * texel;
    const float sw = cx * fx + cy * fy + cz * fz;
    const float tx = rx * su + ux * sv + fx * sw;
    const float ty = ry * su + uy * sv + fy * sw;
    const float tz = rz * su + uz * sv + fz * sw;
    const float dist = extent * 4.0f;
    const float ex = tx - dirX * dist;
    const float ey = ty - dirY * dist;
    const float ez = tz - dirZ * dist;
    sun.view = mat4LookAtYUpRh(ex, ey, ez, tx, ty, tz);
    const float zNear = extent * 0.05f;
    const float zFar = extent * 8.0f;
    sun.proj = mat4OrthoRh(-extent, extent, -extent, extent, zFar, zNear);
    sun.viewProj = mat4Mul(sun.proj, sun.view);
    sun.resolution = kSunShadowResolution;
    sun.radius = extent;
    return sun;
}

// Ortho around the bounding sphere of one view-frustum slice.
// The sphere follows the slice, so the side of the screen stays in the map.
// casterPad pulls in buildings that stand just outside the slice and still cast into it.
inline SunShadow sunShadowSlice(const CameraCull& cam, float dirX, float dirY, float dirZ, float splitNear, float splitFar, float casterPad, float snapCells) {
    const float nearD = splitNear > 0.05f ? splitNear : 0.05f;
    const float farD = splitFar > nearD + 1.0f ? splitFar : nearD + 1.0f;
    const float dist[2] = {nearD, farD};
    float corner[8][3]{};
    float accX = 0.0f;
    float accY = 0.0f;
    float accZ = 0.0f;
    int n = 0;
    for (int plane = 0; plane < 2; ++plane) {
        const float d = dist[plane];
        for (int iy = -1; iy <= 1; iy += 2) {
            for (int ix = -1; ix <= 1; ix += 2) {
                const float sx = static_cast<float>(ix) * d * cam.tanHalfFovX;
                const float sy = static_cast<float>(iy) * d * cam.tanHalfFovY;
                corner[n][0] = cam.pos[0] + cam.forward[0] * d + cam.right[0] * sx + cam.up[0] * sy;
                corner[n][1] = cam.pos[1] + cam.forward[1] * d + cam.right[1] * sx + cam.up[1] * sy;
                corner[n][2] = cam.pos[2] + cam.forward[2] * d + cam.right[2] * sx + cam.up[2] * sy;
                accX += corner[n][0];
                accY += corner[n][1];
                accZ += corner[n][2];
                ++n;
            }
        }
    }
    const float cx = accX / 8.0f;
    const float cy = accY / 8.0f;
    const float cz = accZ / 8.0f;
    float radius = casterPad;
    for (int i = 0; i < 8; ++i) {
        const float dx = corner[i][0] - cx;
        const float dy = corner[i][1] - cy;
        const float dz = corner[i][2] - cz;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > radius) {
            radius = len;
        }
    }
    radius = std::ceil(std::max(radius, 8.0f));
    return sunShadowBuildAt(cx, cy, cz, dirX, dirY, dirZ, radius, snapCells);
}

inline SunShadow sunShadowBuild(const CameraCull& cam, float dirX, float dirY, float dirZ, float extent = 20.0f) {
    return sunShadowBuildAt(cam.pos[0], cam.pos[1], cam.pos[2], dirX, dirY, dirZ, extent);
}

} // namespace burnhope
