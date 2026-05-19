#pragma once

// DirectXMath Compatibility Header
// Replaces glm functionality with DirectXMath equivalents

// Include SAL compatibility header first (needed for Linux)
#include "SALCompat.h"

#if !defined(_WIN32) && !defined(_WIN64)
    // Фикс для Linux: отключает использование sal.h внутри DirectXMath
    #ifndef _XBOX_ONE
        #define _XBOX_ONE
    #endif
#endif

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <cstdint>
namespace burnhope {

// Type aliases for compatibility
using float2 = DirectX::XMFLOAT2;
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;
using float4x4 = DirectX::XMFLOAT4X4;
using quat = DirectX::XMFLOAT4; // Quaternion stored as x,y,z,w
using Int4 = DirectX::XMINT4;
using UInt4 = DirectX::XMUINT4;
using ufloat4 = DirectX::XMUINT4; // Compatibility alias for unsigned int vector4

// Inline helper functions for common operations
inline DirectX::XMVECTOR XM_CALLCONV LoadFloat3(const DirectX::XMFLOAT3& v) {
    return DirectX::XMLoadFloat3(&v);
}

inline DirectX::XMVECTOR XM_CALLCONV LoadFloat4(const DirectX::XMFLOAT4& v) {
    return DirectX::XMLoadFloat4(&v);
}

inline void XM_CALLCONV StoreFloat3(DirectX::XMFLOAT3& dest, DirectX::FXMVECTOR vec) {
    DirectX::XMStoreFloat3(&dest, vec);
}

inline void XM_CALLCONV StoreFloat4(DirectX::XMFLOAT4& dest, DirectX::FXMVECTOR vec) {
    DirectX::XMStoreFloat4(&dest, vec);
}

// Vector operations (returning float3/float4 for storage)
inline float3 Normalize(const float3& v) {
    DirectX::XMVECTOR vec = LoadFloat3(v);
    float3 result;
    StoreFloat3(result, DirectX::XMVector3Normalize(vec));
    return result;
}

inline float4 Normalize(const float4& v) {
    DirectX::XMVECTOR vec = LoadFloat4(v);
    float4 result;
    StoreFloat4(result, DirectX::XMVector4Normalize(vec));
    return result;
}

inline float Dot(const float3& a, const float3& b) {
    DirectX::XMVECTOR va = LoadFloat3(a);
    DirectX::XMVECTOR vb = LoadFloat3(b);
    return DirectX::XMVectorGetX(DirectX::XMVector3Dot(va, vb));
}

inline float Dot(const float4& a, const float4& b) {
    DirectX::XMVECTOR va = LoadFloat4(a);
    DirectX::XMVECTOR vb = LoadFloat4(b);
    return DirectX::XMVectorGetX(DirectX::XMVector4Dot(va, vb));
}

inline float3 Cross(const float3& a, const float3& b) {
    DirectX::XMVECTOR va = LoadFloat3(a);
    DirectX::XMVECTOR vb = LoadFloat3(b);
    float3 result;
    StoreFloat3(result, DirectX::XMVector3Cross(va, vb));
    return result;
}

inline float Length(const float3& v) {
    DirectX::XMVECTOR vec = LoadFloat3(v);
    return DirectX::XMVectorGetX(DirectX::XMVector3Length(vec));
}

inline float LengthSq(const float3& v) {
    DirectX::XMVECTOR vec = LoadFloat3(v);
    return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vec));
}

inline float Distance(const float3& a, const float3& b) {
    float3 diff{a.x - b.x, a.y - b.y, a.z - b.z};
    return Length(diff);
}

// Matrix operations
inline float4x4 MatrixIdentity() {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixIdentity());
    return result;
}

inline float4x4 MatrixTranslation(float x, float y, float z) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixTranslation(x, y, z));
    return result;
}

inline float4x4 MatrixTranslation(const float3& translation) {
    return MatrixTranslation(translation.x, translation.y, translation.z);
}

inline float4x4 MatrixScaling(float x, float y, float z) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixScaling(x, y, z));
    return result;
}

inline float4x4 MatrixScaling(const float3& scale) {
    return MatrixScaling(scale.x, scale.y, scale.z);
}

inline float4x4 MatrixRotationX(float angle) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationX(angle));
    return result;
}

inline float4x4 MatrixRotationY(float angle) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationY(angle));
    return result;
}

inline float4x4 MatrixRotationZ(float angle) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationZ(angle));
    return result;
}

inline float4x4 MatrixRotationAxis(const float3& axis, float angle) {
    float4x4 result;
    DirectX::XMVECTOR axisVec = LoadFloat3(axis);
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationAxis(axisVec, angle));
    return result;
}

inline float4x4 MatrixRotationRollPitchYaw(float pitch, float yaw, float roll) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, roll));
    return result;
}

inline float4x4 MatrixRotationRollPitchYaw(const float3& angles) {
    return MatrixRotationRollPitchYaw(angles.x, angles.y, angles.z);
}

inline float4x4 MatrixLookAtLH(const float3& eye, const float3& at, const float3& up) {
    float4x4 result;
    DirectX::XMVECTOR eyeVec = LoadFloat3(eye);
    DirectX::XMVECTOR atVec = LoadFloat3(at);
    DirectX::XMVECTOR upVec = LoadFloat3(up);
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixLookAtLH(eyeVec, atVec, upVec));
    return result;
}

inline float4x4 MatrixLookAtRH(const float3& eye, const float3& at, const float3& up) {
    float4x4 result;
    DirectX::XMVECTOR eyeVec = LoadFloat3(eye);
    DirectX::XMVECTOR atVec = LoadFloat3(at);
    DirectX::XMVECTOR upVec = LoadFloat3(up);
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixLookAtRH(eyeVec, atVec, upVec));
    return result;
}

inline float4x4 MatrixPerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ));
    return result;
}

inline float4x4 MatrixPerspectiveFovRH(float fovY, float aspect, float nearZ, float farZ) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixPerspectiveFovRH(fovY, aspect, nearZ, farZ));
    return result;
}

inline float4x4 MatrixOrthographicLH(float width, float height, float nearZ, float farZ) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixOrthographicLH(width, height, nearZ, farZ));
    return result;
}

inline float4x4 MatrixOrthographicRH(float width, float height, float nearZ, float farZ) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixOrthographicRH(width, height, nearZ, farZ));
    return result;
}

inline float4x4 MatrixOrthographicOffCenterLH(float left, float right, float bottom, float top, float nearZ, float farZ) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearZ, farZ));
    return result;
}

inline float4x4 MatrixOrthographicOffCenterRH(float left, float right, float bottom, float top, float nearZ, float farZ) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixOrthographicOffCenterRH(left, right, bottom, top, nearZ, farZ));
    return result;
}

// Matrix multiplication
inline float4x4 MatrixMultiply(const float4x4& a, const float4x4& b) {
    float4x4 result;
    DirectX::XMMATRIX ma = DirectX::XMLoadFloat4x4(&a);
    DirectX::XMMATRIX mb = DirectX::XMLoadFloat4x4(&b);
    DirectX::XMStoreFloat4x4(&result, ma * mb);
    return result;
}

// Inverse and transpose
inline float4x4 MatrixInverse(const float4x4& m) {
    float4x4 result;
    DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&m);
    DirectX::XMVECTOR det;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixInverse(&det, mat));
    return result;
}

inline float4x4 MatrixTranspose(const float4x4& m) {
    float4x4 result;
    DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&m);
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixTranspose(mat));
    return result;
}

// Transform operations
inline float3 TransformPoint(const float3& p, const float4x4& m) {
    DirectX::XMVECTOR vec = DirectX::XMLoadFloat3(&p);
    vec = DirectX::XMVectorSetW(vec, 1.0f);
    DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&m);
    float3 result;
    StoreFloat3(result, DirectX::XMVector3TransformCoord(vec, mat));
    return result;
}

inline float3 TransformVector(const float3& v, const float4x4& m) {
    DirectX::XMVECTOR vec = DirectX::XMLoadFloat3(&v);
    DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&m);
    float3 result;
    StoreFloat3(result, DirectX::XMVector3TransformNormal(vec, mat));
    return result;
}

inline float4 TransformFloat4(const float4& v, const float4x4& m) {
    DirectX::XMVECTOR vec = DirectX::XMLoadFloat4(&v);
    DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&m);
    float4 result;
    StoreFloat4(result, DirectX::XMVector4Transform(vec, mat));
    return result;
}

// Quaternion operations
inline quat QuaternionIdentity() {
    return quat{0.0f, 0.0f, 0.0f, 1.0f};
}

inline quat QuaternionRotationAxis(const float3& axis, float angle) {
    quat result;
    DirectX::XMVECTOR axisVec = LoadFloat3(axis);
    StoreFloat4(result, DirectX::XMQuaternionRotationAxis(axisVec, angle));
    return result;
}

inline quat QuaternionRotationRollPitchYaw(float pitch, float yaw, float roll) {
    quat result;
    StoreFloat4(result, DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return result;
}

inline quat QuaternionMultiply(const quat& a, const quat& b) {
    quat result;
    DirectX::XMVECTOR qa = LoadFloat4(a);
    DirectX::XMVECTOR qb = LoadFloat4(b);
    StoreFloat4(result, DirectX::XMQuaternionMultiply(qa, qb));
    return result;
}

inline quat QuaternionSlerp(const quat& a, const quat& b, float t) {
    quat result;
    DirectX::XMVECTOR qa = LoadFloat4(a);
    DirectX::XMVECTOR qb = LoadFloat4(b);
    StoreFloat4(result, DirectX::XMQuaternionSlerp(qa, qb, t));
    return result;
}

inline quat QuaternionNormalize(const quat& q) {
    quat result;
    StoreFloat4(result, DirectX::XMQuaternionNormalize(LoadFloat4(q)));
    return result;
}

inline float4x4 QuaternionToMatrix(const quat& q) {
    float4x4 result;
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationQuaternion(LoadFloat4(q)));
    return result;
}

inline quat QuaternionFromEuler(const float3& euler) {
    // euler angles in radians (pitch, yaw, roll)
    DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(euler.x, euler.y, euler.z);
    quat result;
    DirectX::XMStoreFloat4(&result, q);
    return result;
}

inline quat QuaternionFromMatrix(const float4x4& m) {
    quat result;
    StoreFloat4(result, DirectX::XMQuaternionRotationMatrix(DirectX::XMLoadFloat4x4(&m)));
    return result;
}

// Utility functions
inline float3 Lerp(const float3& a, const float3& b, float t) {
    return float3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

inline float4 Lerp(const float4& a, const float4& b, float t) {
    return float4{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    };
}

inline float Clamp(float val, float minVal, float maxVal) {
    return (val < minVal) ? minVal : (val > maxVal) ? maxVal : val;
}

inline float3 Min(const float3& a, const float3& b) {
    return float3{
        (a.x < b.x) ? a.x : b.x,
        (a.y < b.y) ? a.y : b.y,
        (a.z < b.z) ? a.z : b.z
    };
}

inline float3 Max(const float3& a, const float3& b) {
    return float3{
        (a.x > b.x) ? a.x : b.x,
        (a.y > b.y) ? a.y : b.y,
        (a.z > b.z) ? a.z : b.z
    };
}

// Constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float PI_OVER_2 = 1.57079632679489661923f;
constexpr float PI_OVER_4 = 0.78539816339744830961f;

inline float Radians(float degrees) {
    return degrees * (PI / 180.0f);
}

inline float Degrees(float radians) {
    return radians * (180.0f / PI);
}

// Component-wise min/max
inline float MinComponent(const float3& v) {
    return (v.x < v.y) ? ((v.x < v.z) ? v.x : v.z) : ((v.y < v.z) ? v.y : v.z);
}

inline float MaxComponent(const float3& v) {
    return (v.x > v.y) ? ((v.x > v.z) ? v.x : v.z) : ((v.y > v.z) ? v.y : v.z);
}

// Packing functions for vertex data
inline uint32_t PackSnorm3x10_1x2(const float4& v) {
    int x = static_cast<int>(Clamp(v.x, -1.0f, 1.0f) * 511.0f);
    int y = static_cast<int>(Clamp(v.y, -1.0f, 1.0f) * 511.0f);
    int z = static_cast<int>(Clamp(v.z, -1.0f, 1.0f) * 511.0f);
    int w = static_cast<int>(Clamp(v.w, -1.0f, 1.0f) * 1.0f);
    return (x & 0x3FF) | ((y & 0x3FF) << 10) | ((z & 0x3FF) << 20) | ((w & 0x3) << 30);
}

inline uint32_t PackHalf2x16(const float2& v) {
    // Simple half-float packing (not IEEE 754 compliant but functional)
    auto toHalf = [](float f) -> uint16_t {
        int32_t i = *reinterpret_cast<int32_t*>(&f);
        int32_t sign = (i >> 31) & 0x1;
        int32_t exp = ((i >> 23) & 0xFF) - 127 + 15;
        int32_t mant = (i >> 13) & 0x3FF;
        if (exp < 0) return static_cast<uint16_t>(sign << 15);
        if (exp > 31) return static_cast<uint16_t>((sign << 15) | (31 << 10) | 0x3FF);
        return static_cast<uint16_t>((sign << 15) | (exp << 10) | mant);
    };
    return (static_cast<uint32_t>(toHalf(v.x)) << 16) | toHalf(v.y);
}

inline float2 UnpackHalf2x16(uint32_t packed) {
    auto fromHalf = [](uint16_t h) -> float {
        int32_t sign = (h >> 15) & 0x1;
        int32_t exp = ((h >> 10) & 0x1F) - 15 + 127;
        int32_t mant = h & 0x3FF;
        int32_t i = (sign << 31) | (exp << 23) | (mant << 13);
        return *reinterpret_cast<float*>(&i);
    };
    return float2{fromHalf(static_cast<uint16_t>(packed >> 16)), fromHalf(static_cast<uint16_t>(packed & 0xFFFF))};
}
// Перегрузка операторов для float2
inline float2 operator+(const float2& a, const float2& b) {
    return float2{a.x + b.x, a.y + b.y};
}

inline float2 operator-(const float2& a, const float2& b) {
    return float2{a.x - b.x, a.y - b.y};
}

inline float2 operator*(const float2& v, float scalar) {
    return float2{v.x * scalar, v.y * scalar};
}

inline float2 operator/(const float2& v, float scalar) {
    return float2{v.x / scalar, v.y / scalar};
}

// Перегрузка оператора + для float3
inline float3 operator+(const float3& a, const float3& b) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat3(&a);
    DirectX::XMVECTOR vb = DirectX::XMLoadFloat3(&b);
    float3 result;
    DirectX::XMStoreFloat3(&result, DirectX::XMVectorAdd(va, vb));
    return result;
}

// Перегрузка оператора += для float3
inline float3& operator+=(float3& a, const float3& b) {
    a = a + b;
    return a;
}

// Перегрузка оператора - для float3
inline float3 operator-(const float3& a, const float3& b) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat3(&a);
    DirectX::XMVECTOR vb = DirectX::XMLoadFloat3(&b);
    float3 result;
    DirectX::XMStoreFloat3(&result, DirectX::XMVectorSubtract(va, vb));
    return result;
}

// Перегрузка оператора * на число (скаляр)
inline float3 operator*(const float3& v, float scalar) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat3(&v);
    float3 result;
    DirectX::XMStoreFloat3(&result, DirectX::XMVectorScale(va, scalar));
    return result;
}

// Перегрузка оператора * для скаляра слева (float * float3)
inline float3 operator*(float scalar, const float3& v) {
    return v * scalar;
}

// Компонентное умножение float3 * float3
inline float3 operator*(const float3& a, const float3& b) {
    return float3{a.x * b.x, a.y * b.y, a.z * b.z};
}

// Перегрузка оператора * для float4 на число
inline float4 operator*(const float4& v, float scalar) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat4(&v);
    float4 result;
    DirectX::XMStoreFloat4(&result, DirectX::XMVectorScale(va, scalar));
    return result;
}

// Перегрузка оператора / для float3 на скаляр
inline float3 operator/(const float3& v, float scalar) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat3(&v);
    float3 result;
    DirectX::XMStoreFloat3(&result, DirectX::XMVectorScale(va, 1.0f / scalar));
    return result;
}

// Компонентное деление float3 / float3
inline float3 operator/(const float3& a, const float3& b) {
    return float3{a.x / b.x, a.y / b.y, a.z / b.z};
}

// Перегрузка оператора / для float4 на скаляр
inline float4 operator/(const float4& v, float scalar) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat4(&v);
    float4 result;
    DirectX::XMStoreFloat4(&result, DirectX::XMVectorScale(va, 1.0f / scalar));
    return result;
}

// Перегрузка оператора + для float4
inline float4 operator+(const float4& a, const float4& b) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat4(&a);
    DirectX::XMVECTOR vb = DirectX::XMLoadFloat4(&b);
    float4 result;
    DirectX::XMStoreFloat4(&result, DirectX::XMVectorAdd(va, vb));
    return result;
}

// Перегрузка оператора - для float4
inline float4 operator-(const float4& a, const float4& b) {
    DirectX::XMVECTOR va = DirectX::XMLoadFloat4(&a);
    DirectX::XMVECTOR vb = DirectX::XMLoadFloat4(&b);
    float4 result;
    DirectX::XMStoreFloat4(&result, DirectX::XMVectorSubtract(va, vb));
    return result;
}

// Унарный оператор - для float3
inline float3 operator-(const float3& v) {
    return float3{-v.x, -v.y, -v.z};
}

// Унарный оператор - для float4
inline float4 operator-(const float4& v) {
    return float4{-v.x, -v.y, -v.z, -v.w};
}

// Оператор умножения матриц float4x4 * float4x4
inline float4x4 operator*(const float4x4& a, const float4x4& b) {
    return MatrixMultiply(a, b);
}

// Оператор умножения матрицы на вектор float4x4 * float4
inline float4 operator*(const float4x4& m, const float4& v) {
    return TransformFloat4(v, m);
}

// Функция Sign
inline float Sign(float x) {
    return (x > 0.0f) ? 1.0f : (x < 0.0f) ? -1.0f : 0.0f;
}

// Вспомогательные конструкторы
inline float4 MakeFloat4(const float3& xyz, float w) {
    return float4{xyz.x, xyz.y, xyz.z, w};
}

inline float3 MakeFloat3(const float4& v) {
    return float3{v.x, v.y, v.z};
}

inline float2 MakeFloat2(const float3& v) {
    return float2{v.x, v.y};
}

inline float3 Float3FromFloat(float f) {
    return float3{f, f, f};
}

inline float4x4 Float4x4Identity() {
    return MatrixIdentity();
}

// Доступ к элементам матрицы (row-major)
inline float GetMatrixElement(const float4x4& m, int row, int col) {
    float data[16] = {m._11, m._12, m._13, m._14,
                      m._21, m._22, m._23, m._24,
                      m._31, m._32, m._33, m._34,
                      m._41, m._42, m._43, m._44};
    return data[row * 4 + col];
}

inline void SetMatrixElement(float4x4& m, int row, int col, float value) {
    float* data = &m._11;
    data[row * 4 + col] = value;
}

// Получение строки матрицы как float4
inline float4 GetMatrixRow(const float4x4& m, int row) {
    if (row == 0) return float4{m._11, m._12, m._13, m._14};
    if (row == 1) return float4{m._21, m._22, m._23, m._24};
    if (row == 2) return float4{m._31, m._32, m._33, m._34};
    return float4{m._41, m._42, m._43, m._44};
}

// Получение позиции из матрицы (4-я строка)
inline float3 GetMatrixPosition(const float4x4& m) {
    return float3{m._41, m._42, m._43};
}

// Умножение кватерниона на вектор (вращение вектора)
inline float3 operator*(const quat& q, const float3& v) {
    float3 qVec{q.x, q.y, q.z};
    float3 uv = Cross(qVec, v);
    float3 uuv = Cross(qVec, uv);
    return v + (uv * q.w + uuv) * 2.0f;
}

// Заглушки для функций упаковки (реализация в другом месте или не нужна)
inline uint32_t packUnorm4x8(const float4& v) {
    uint32_t r = static_cast<uint32_t>(Clamp(v.x, 0.0f, 1.0f) * 255.0f);
    uint32_t g = static_cast<uint32_t>(Clamp(v.y, 0.0f, 1.0f) * 255.0f);
    uint32_t b = static_cast<uint32_t>(Clamp(v.z, 0.0f, 1.0f) * 255.0f);
    uint32_t a = static_cast<uint32_t>(Clamp(v.w, 0.0f, 1.0f) * 255.0f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

inline uint32_t PackUnorm4x8(const float4& v) {
    return packUnorm4x8(v);
}

inline uint32_t packSnorm3x10_1x2(const float4& v) {
    // Упрощенная заглушка - используем packUnorm4x8
    return packUnorm4x8(float4{v.x * 0.5f + 0.5f, v.y * 0.5f + 0.5f, v.z * 0.5f + 0.5f, v.w * 0.5f + 0.5f});
}

inline uint32_t packHalf2x16(const float2& v) {
    // Заглушка - просто упаковываем как два uint16
    uint32_t x = static_cast<uint32_t>(Clamp(v.x, 0.0f, 1.0f) * 65535.0f);
    uint32_t y = static_cast<uint32_t>(Clamp(v.y, 0.0f, 1.0f) * 65535.0f);
    return (y << 16) | x;
}

// quat_cast заглушка
inline quat quat_cast(const float4x4& m) {
    return QuaternionFromMatrix(m);
}

} // namespace burnhope
