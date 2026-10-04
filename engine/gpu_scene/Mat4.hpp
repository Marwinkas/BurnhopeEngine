#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace burnhope {

struct Mat4 {
    float m[16]{}; // column-major, matches Slang/HLSL default
};

inline Mat4 mat4Identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

inline Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.m[c * 4 + row] =
                a.m[0 * 4 + row] * b.m[c * 4 + 0]
                + a.m[1 * 4 + row] * b.m[c * 4 + 1]
                + a.m[2 * 4 + row] * b.m[c * 4 + 2]
                + a.m[3 * 4 + row] * b.m[c * 4 + 3];
        }
    }
    return r;
}

inline Mat4 mat4PerspectiveYUpRh(float fovYRad, float aspect, float zn, float zf) {
    Mat4 r{};
    const float th = std::tan(fovYRad * 0.5f);
    r.m[0] = 1.0f / (aspect * th);
    r.m[5] = -1.0f / th; // Vulkan clip Y → framebuffer
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}

// Infinite reverse-Z. Near is 1, far is 0. Point and sun shadows stay on the finite matrix.
inline Mat4 mat4PerspectiveReverseZ(float fovYRad, float aspect, float zn) {
    Mat4 r{};
    const float th = std::tan(fovYRad * 0.5f);
    r.m[0] = 1.0f / (aspect * th);
    r.m[5] = -1.0f / th;
    r.m[10] = 0.0f;
    r.m[11] = -1.0f;
    r.m[14] = zn;
    return r;
}

inline Mat4 mat4LookAtYUpRh(float ex, float ey, float ez, float tx, float ty, float tz) {
    float fx = tx - ex;
    float fy = ty - ey;
    float fz = tz - ez;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl;
    fy /= fl;
    fz /= fl;
    float upx = 0.0f;
    float upy = 1.0f;
    float upz = 0.0f;
    if (std::fabs(fy) > 0.999f) {
        upx = 0.0f;
        upy = 0.0f;
        upz = fy > 0.0f ? -1.0f : 1.0f;
    }
    float sx = fy * upz - fz * upy;
    float sy = fz * upx - fx * upz;
    float sz = fx * upy - fy * upx;
    const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl;
    sy /= sl;
    sz /= sl;
    const float ux = sy * fz - sz * fy;
    const float uy = sz * fx - sx * fz;
    const float uz = sx * fy - sy * fx;

    Mat4 r = mat4Identity();
    r.m[0] = sx;
    r.m[1] = ux;
    r.m[2] = -fx;
    r.m[4] = sy;
    r.m[5] = uy;
    r.m[6] = -fy;
    r.m[8] = sz;
    r.m[9] = uz;
    r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux * ex + uy * ey + uz * ez);
    r.m[14] = -(-fx * ex - fy * ey - fz * ez);
    return r;
}

inline Mat4 mat4Inverse(const Mat4& in) {
    const float* m = in.m;
    Mat4 out{};
    float* o = out.m;

    o[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
        + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    o[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
        - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    o[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
        + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    o[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
        - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    o[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
        - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    o[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
        + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    o[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
        - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    o[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
        + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    o[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
        + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    o[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
        - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    o[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
        + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    o[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
        - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    o[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
        - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    o[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
        + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    o[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
        - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    o[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
        + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det = m[0] * o[0] + m[1] * o[4] + m[2] * o[8] + m[3] * o[12];
    const float invDet = (std::fabs(det) < 1e-12f) ? 1.0f : (1.0f / det);
    for (float& v : out.m) {
        v *= invDet;
    }
    return out;
}

} // namespace burnhope
