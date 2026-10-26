#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace burnhope {

struct Mat4 {
    float m[16]{}; // column-major, matches Slang/HLSL default
};

inline void vec3Add(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

inline void vec3Sub(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

inline float vec3Dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float vec3Len(const float a[3]) {
    return std::sqrt(vec3Dot(a, a));
}

// Нормализует на месте и возвращает прежнюю длину. Нулевой вектор остаётся нулевым.
inline float vec3Normalize(float v[3]) {
    const float len = vec3Len(v);
    if (len > 1.0e-8f) {
        const float inv = 1.0f / len;
        v[0] *= inv;
        v[1] *= inv;
        v[2] *= inv;
    }
    return len;
}

inline void vec3Cross(const float a[3], const float b[3], float out[3]) {
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

inline float vec3Dist(const float a[3], const float b[3]) {
    float d[3];
    vec3Sub(a, b, d);
    return vec3Len(d);
}

// Оси объекта по углам Эйлера в градусах (x = pitch, y = yaw, z = roll).
inline void mat3FromEuler(const float euler[3], float x[3], float y[3], float z[3]) {
    const float yaw = euler[1] * 0.017453292f;
    const float pitch = euler[0] * 0.017453292f;
    const float roll = euler[2] * 0.017453292f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cr = std::cos(roll);
    const float sr = std::sin(roll);
    x[0] = cy * cr + sy * sp * sr;
    x[1] = cp * sr;
    x[2] = -sy * cr + cy * sp * sr;
    y[0] = -cy * sr + sy * sp * cr;
    y[1] = cp * cr;
    y[2] = sy * sr + cy * sp * cr;
    z[0] = sy * cp;
    z[1] = -sp;
    z[2] = cy * cp;
}

inline void eulerFromMat3(const float x[3], const float y[3], const float z[3], float euler[3]) {
    euler[0] = std::asin(std::fmax(-1.0f, std::fmin(1.0f, -z[1]))) * 57.2957795f;
    if (std::fabs(z[1]) < 0.999f) {
        euler[1] = std::atan2(z[0], z[2]) * 57.2957795f;
        euler[2] = std::atan2(x[1], y[1]) * 57.2957795f;
    } else {
        euler[1] = std::atan2(-x[2], x[0]) * 57.2957795f;
        euler[2] = 0.0f;
    }
}

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

// Reverse-Z orthographic. Near maps to 1, far to 0. Y is flipped for Vulkan.
inline Mat4 mat4OrthoReverseZ(float halfX, float halfY, float zn, float zf) {
    Mat4 r{};
    const float hx = halfX > 1.0e-4f ? halfX : 1.0e-4f;
    const float hy = halfY > 1.0e-4f ? halfY : 1.0e-4f;
    const float span = zf > zn ? zf - zn : 1.0f;
    r.m[0] = 1.0f / hx;
    r.m[5] = -1.0f / hy;
    r.m[10] = 1.0f / span;
    r.m[14] = zf / span;
    r.m[15] = 1.0f;
    return r;
}

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

// Reverse-Z orthographic box symmetric in X/Y: near maps to 1, far to 0 (z swapped in mat4OrthoRh).
inline Mat4 mat4OrthoBoxReverseZ(float halfX, float halfY, float zn, float zf) {
    return mat4OrthoRh(-halfX, halfX, -halfY, halfY, zf, zn);
}

inline void mat4Orthonormal(
    float fx, float fy, float fz,
    float upx, float upy, float upz,
    float& sx, float& sy, float& sz,
    float& ux, float& uy, float& uz) {
    sx = fy * upz - fz * upy;
    sy = fz * upx - fx * upz;
    sz = fx * upy - fy * upx;
    const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    const float inv = sl > 1.0e-8f ? 1.0f / sl : 0.0f;
    sx *= inv;
    sy *= inv;
    sz *= inv;
    ux = sy * fz - sz * fy;
    uy = sz * fx - sx * fz;
    uz = sx * fy - sy * fx;
}

inline Mat4 mat4LookDir(
    float ex, float ey, float ez,
    float fx, float fy, float fz,
    float upx, float upy, float upz) {
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    const float inv = fl > 1.0e-8f ? 1.0f / fl : 0.0f;
    fx *= inv;
    fy *= inv;
    fz *= inv;
    float sx, sy, sz, ux, uy, uz;
    mat4Orthonormal(fx, fy, fz, upx, upy, upz, sx, sy, sz, ux, uy, uz);
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
    r.m[14] = fx * ex + fy * ey + fz * ez;
    return r;
}

inline Mat4 mat4LookAtYUpRh(float ex, float ey, float ez, float tx, float ty, float tz) {
    float fx = tx - ex;
    float fy = ty - ey;
    float fz = tz - ez;
    float upx = 0.0f;
    float upy = 1.0f;
    float upz = 0.0f;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    const float inv = fl > 1.0e-8f ? 1.0f / fl : 0.0f;
    if (std::fabs(fy * inv) > 0.999f) {
        upx = 0.0f;
        upy = 0.0f;
        upz = fy > 0.0f ? -1.0f : 1.0f;
    }
    return mat4LookDir(ex, ey, ez, fx, fy, fz, upx, upy, upz);
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

// Ортонормированная пара u, v поперёк оси. Опора — мировой верх, у почти вертикальной оси — мировой X.
// Ложь, если ось вырождена. u = norm(axis × ref), v = axis × u.
[[nodiscard]] inline bool mat4BasisFromAxis(const float axis[3], float u[3], float v[3]) {
    const bool steep = std::fabs(axis[1]) >= 0.9f;
    const float ref[3] = {steep ? 1.0f : 0.0f, steep ? 0.0f : 1.0f, 0.0f};
    u[0] = axis[1] * ref[2] - axis[2] * ref[1];
    u[1] = axis[2] * ref[0] - axis[0] * ref[2];
    u[2] = axis[0] * ref[1] - axis[1] * ref[0];
    const float len = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (len < 1.0e-4f) {
        return false;
    }
    u[0] /= len;
    u[1] /= len;
    u[2] /= len;
    v[0] = axis[1] * u[2] - axis[2] * u[1];
    v[1] = axis[2] * u[0] - axis[0] * u[2];
    v[2] = axis[0] * u[1] - axis[1] * u[0];
    return true;
}

} // namespace burnhope
