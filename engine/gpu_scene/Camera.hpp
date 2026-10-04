#pragma once

#include "gpu_scene/Mat4.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace burnhope {

// One pose. Later phases read CameraCull, they do not recompute look-at.
struct CameraPose {
    float eye[3]{0.0f, 0.35f, 2.4f};
    float target[3]{0.0f, 0.05f, 0.0f};
    float fovYRad = 1.0471975512f;
    float zn = 0.1f;
    float zf = 50.0f;
    float aspect = 1.0f;
    float jitterX = 0.0f; // NDC added into projection, TAA / phase 13
    float jitterY = 0.0f;
};

// Everything a later cull phase consumes. CPU frustum, cone, cascades, froxels,
// Hi-Z reprojection and motion vectors all read this blob. 4 cascade splits and
// 16 froxel slices are the counts those phases already named.
struct CameraCull {
    Mat4 view{};
    Mat4 proj{};
    Mat4 viewProj{};
    Mat4 invView{};
    Mat4 invProj{};
    Mat4 invViewProj{};
    Mat4 prevViewProj{};
    float pos[4]{};
    float forward[4]{};
    float right[4]{};
    float up[4]{};
    float planes[6][4]{}; // inward: left right bottom top near far
    float zn = 0.1f;
    float zf = 50.0f;
    float fovY = 1.0f;
    float aspect = 1.0f;
    float tanHalfFovX = 1.0f;
    float tanHalfFovY = 1.0f;
    float jitter[2]{};
    float cascadeSplit[5]{}; // zn .. zf, 4 cascades
    float froxelEdge[17]{};  // 16 slices, view-space z
    uint32_t cascadeCount = 4;
    uint32_t froxelCount = 16;
};

static_assert(std::is_trivially_copyable_v<CameraPose>);
static_assert(std::is_trivially_copyable_v<CameraCull>);

inline void cameraRow(const Mat4& m, int row, float& x, float& y, float& z, float& w) {
    x = m.m[row];
    y = m.m[4 + row];
    z = m.m[8 + row];
    w = m.m[12 + row];
}

inline void cameraPlane(float* p, float x, float y, float z, float w) {
    const float len = std::sqrt(x * x + y * y + z * z);
    const float s = len > 1.0e-8f ? 1.0f / len : 0.0f;
    p[0] = x * s;
    p[1] = y * s;
    p[2] = z * s;
    p[3] = w * s;
}

inline CameraCull cameraCullBuild(const CameraPose& pose, const Mat4* prevViewProj) {
    CameraCull c{};
    const float aspect = pose.aspect > 1.0e-4f ? pose.aspect : 1.0f;
    const float zn = pose.zn > 1.0e-4f ? pose.zn : 0.1f;
    const float zf = pose.zf > zn ? pose.zf : zn + 1.0f;
    c.view = mat4LookAtYUpRh(pose.eye[0], pose.eye[1], pose.eye[2], pose.target[0], pose.target[1], pose.target[2]);
    c.proj = mat4PerspectiveReverseZ(pose.fovYRad, aspect, zn);
    c.proj.m[8] += pose.jitterX;
    c.proj.m[9] += pose.jitterY;
    c.viewProj = mat4Mul(c.proj, c.view);
    c.invView = mat4Inverse(c.view);
    c.invProj = mat4Inverse(c.proj);
    c.invViewProj = mat4Inverse(c.viewProj);
    c.prevViewProj = prevViewProj != nullptr ? *prevViewProj : c.viewProj;
    c.pos[0] = pose.eye[0];
    c.pos[1] = pose.eye[1];
    c.pos[2] = pose.eye[2];
    c.pos[3] = 1.0f;
    float fx = pose.target[0] - pose.eye[0];
    float fy = pose.target[1] - pose.eye[1];
    float fz = pose.target[2] - pose.eye[2];
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    const float inv = fl > 1.0e-8f ? 1.0f / fl : 0.0f;
    c.forward[0] = fx * inv;
    c.forward[1] = fy * inv;
    c.forward[2] = fz * inv;
    c.right[0] = c.view.m[0];
    c.right[1] = c.view.m[4];
    c.right[2] = c.view.m[8];
    c.up[0] = c.view.m[1];
    c.up[1] = c.view.m[5];
    c.up[2] = c.view.m[9];
    c.zn = zn;
    c.zf = zf;
    c.fovY = pose.fovYRad;
    c.aspect = aspect;
    c.tanHalfFovY = std::tan(pose.fovYRad * 0.5f);
    c.tanHalfFovX = c.tanHalfFovY * aspect;
    c.jitter[0] = pose.jitterX;
    c.jitter[1] = pose.jitterY;

    float r0x, r0y, r0z, r0w, r1x, r1y, r1z, r1w, r2x, r2y, r2z, r2w, r3x, r3y, r3z, r3w;
    cameraRow(c.viewProj, 0, r0x, r0y, r0z, r0w);
    cameraRow(c.viewProj, 1, r1x, r1y, r1z, r1w);
    cameraRow(c.viewProj, 2, r2x, r2y, r2z, r2w);
    cameraRow(c.viewProj, 3, r3x, r3y, r3z, r3w);
    cameraPlane(c.planes[0], r3x + r0x, r3y + r0y, r3z + r0z, r3w + r0w);
    cameraPlane(c.planes[1], r3x - r0x, r3y - r0y, r3z - r0z, r3w - r0w);
    cameraPlane(c.planes[2], r3x + r1x, r3y + r1y, r3z + r1z, r3w + r1w);
    cameraPlane(c.planes[3], r3x - r1x, r3y - r1y, r3z - r1z, r3w - r1w);
    cameraPlane(c.planes[4], r3x - r2x, r3y - r2y, r3z - r2z, r3w - r2w);
    cameraPlane(c.planes[5], r2x, r2y, r2z, r2w);

    constexpr int kCascades = 4;
    c.cascadeCount = kCascades;
    for (int i = 0; i <= kCascades; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(kCascades);
        const float logSplit = zn * std::pow(zf / zn, p);
        const float uniSplit = zn + (zf - zn) * p;
        c.cascadeSplit[i] = uniSplit + (logSplit - uniSplit) * 0.5f;
    }
    c.froxelCount = 16;
    for (uint32_t i = 0; i <= c.froxelCount; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(c.froxelCount);
        c.froxelEdge[i] = zn * std::pow(zf / zn, p);
    }
    return c;
}

inline bool cameraSphereVisible(const CameraCull& c, float x, float y, float z, float radius) {
    for (int i = 0; i < 6; ++i) {
        const float d = c.planes[i][0] * x + c.planes[i][1] * y + c.planes[i][2] * z + c.planes[i][3];
        if (d < -radius) {
            return false;
        }
    }
    return true;
}

// Cone points along (dx,dy,dz). cutoff is the cosine of the cone half-angle.
// A cluster whose whole cone faces away from the eye is rejected (phase 11/13 meshlet cone).
inline bool cameraConeVisible(const CameraCull& c, float x, float y, float z, float dx, float dy, float dz, float cutoff) {
    float vx = c.pos[0] - x;
    float vy = c.pos[1] - y;
    float vz = c.pos[2] - z;
    const float vl = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (vl < 1.0e-6f) {
        return true;
    }
    vx /= vl;
    vy /= vl;
    vz /= vl;
    const float facing = vx * dx + vy * dy + vz * dz;
    return facing >= cutoff;
}

inline int cameraFroxelSlice(const CameraCull& c, float viewZ) {
    const float z = std::clamp(viewZ, c.zn, c.zf);
    for (uint32_t i = 0; i < c.froxelCount; ++i) {
        if (z <= c.froxelEdge[i + 1]) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(c.froxelCount - 1);
}

} // namespace burnhope
