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

inline SunShadow sunShadowBuildAt(float cx, float cy, float cz, float dirX, float dirY, float dirZ, float extent = 20.0f, float snapCells = 1.0f) {
    SunShadow sun{};
    const float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    const float s = len > 1.0e-6f ? 1.0f / len : 1.0f;
    dirX *= s;
    dirY *= s;
    dirZ *= s;
    float upx = 0.0f;
    float upy = 1.0f;
    float upz = 0.0f;
    if (std::fabs(dirY) > 0.999f) {
        upy = 0.0f;
        upz = dirY > 0.0f ? -1.0f : 1.0f;
    }
    float rx, ry, rz, ux, uy, uz;
    mat4Orthonormal(dirX, dirY, dirZ, upx, upy, upz, rx, ry, rz, ux, uy, uz);
    const float cells = snapCells > 1.0f ? snapCells : 1.0f;
    const float texel = (2.0f * extent) / static_cast<float>(kSunShadowResolution) * cells;
    const float su = std::round((cx * rx + cy * ry + cz * rz) / texel) * texel;
    const float sv = std::round((cx * ux + cy * uy + cz * uz) / texel) * texel;
    const float sw = cx * dirX + cy * dirY + cz * dirZ;
    const float tx = rx * su + ux * sv + dirX * sw;
    const float ty = ry * su + uy * sv + dirY * sw;
    const float tz = rz * su + uz * sv + dirZ * sw;
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

// Physical 2048 is one page window of a larger virtual cascade.
// The window is the light-space box of the slice, snapped to 16 pages and clamped
// so a long cascade does not stretch one texel across metres.
inline SunShadow sunShadowWindow(
    float dirX, float dirY, float dirZ,
    const float corner[8][3],
    float casterPad,
    float maxHalf) {
    SunShadow sun{};
    const float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    const float s = len > 1.0e-6f ? 1.0f / len : 1.0f;
    dirX *= s;
    dirY *= s;
    dirZ *= s;
    float upx = 0.0f;
    float upy = 1.0f;
    float upz = 0.0f;
    if (std::fabs(dirY) > 0.999f) {
        upy = 0.0f;
        upz = dirY > 0.0f ? -1.0f : 1.0f;
    }
    float rx, ry, rz, ux, uy, uz;
    mat4Orthonormal(dirX, dirY, dirZ, upx, upy, upz, rx, ry, rz, ux, uy, uz);
    float minU = 1.0e30f;
    float maxU = -1.0e30f;
    float minV = 1.0e30f;
    float maxV = -1.0e30f;
    float sw = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const float u = corner[i][0] * rx + corner[i][1] * ry + corner[i][2] * rz;
        const float v = corner[i][0] * ux + corner[i][1] * uy + corner[i][2] * uz;
        minU = std::fmin(minU, u);
        maxU = std::fmax(maxU, u);
        minV = std::fmin(minV, v);
        maxV = std::fmax(maxV, v);
        sw += corner[i][0] * dirX + corner[i][1] * dirY + corner[i][2] * dirZ;
    }
    sw /= 8.0f;
    const float pad = casterPad > 0.0f ? casterPad : 0.0f;
    minU -= pad;
    maxU += pad;
    minV -= pad;
    maxV += pad;
    const float cap = maxHalf > 4.0f ? maxHalf : 4.0f;
    const float page = (2.0f * cap) / 16.0f;
    auto snapDown = [page](float v) { return std::floor(v / page) * page; };
    auto snapUp = [page](float v) { return std::ceil(v / page) * page; };
    minU = snapDown(minU);
    maxU = snapUp(maxU);
    minV = snapDown(minV);
    maxV = snapUp(maxV);
    float midU = (minU + maxU) * 0.5f;
    float midV = (minV + maxV) * 0.5f;
    float halfU = (maxU - minU) * 0.5f;
    float halfV = (maxV - minV) * 0.5f;
    halfU = std::max(halfU, page);
    halfV = std::max(halfV, page);
    const float depthR = std::max(std::max(halfU, halfV), cap * 0.5f);
    const float tx = rx * midU + ux * midV + dirX * sw;
    const float ty = ry * midU + uy * midV + dirY * sw;
    const float tz = rz * midU + uz * midV + dirZ * sw;
    const float dist = depthR * 4.0f;
    sun.view = mat4LookAtYUpRh(tx - dirX * dist, ty - dirY * dist, tz - dirZ * dist, tx, ty, tz);
    const float zNear = depthR * 0.05f;
    const float zFar = depthR * 8.0f;
    sun.proj = mat4OrthoRh(-halfU, halfU, -halfV, halfV, zFar, zNear);
    sun.viewProj = mat4Mul(sun.proj, sun.view);
    sun.resolution = kSunShadowResolution;
    sun.radius = std::max(halfU, halfV);
    return sun;
}

// Ortho around the bounding sphere of one view-frustum slice.
// The sphere follows the slice, so the side of the screen stays in the map.
// casterPad pulls in buildings that stand just outside the slice and still cast into it.
inline SunShadow sunShadowSlice(const CameraCull& cam, float dirX, float dirY, float dirZ, float splitNear, float splitFar, float casterPad, float snapCells, float maxHalf = 1.0e6f) {
    (void)snapCells;
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
    const float cap = maxHalf < 1.0e5f ? maxHalf : radius;
    return sunShadowWindow(dirX, dirY, dirZ, corner, casterPad, cap);
}

inline SunShadow sunShadowBuild(const CameraCull& cam, float dirX, float dirY, float dirZ, float extent = 20.0f) {
    return sunShadowBuildAt(cam.pos[0], cam.pos[1], cam.pos[2], dirX, dirY, dirZ, extent);
}

} // namespace burnhope
