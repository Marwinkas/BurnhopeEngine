#pragma once

#include "input/Input.hpp"

#include <cmath>
#include <cstdint>

namespace burnhope {

constexpr uint32_t kFlyKeyW = 26;
constexpr uint32_t kFlyKeyA = 4;
constexpr uint32_t kFlyKeyS = 22;
constexpr uint32_t kFlyKeyD = 7;
constexpr uint32_t kFlyKeyE = 8;
constexpr uint32_t kFlyKeyQ = 20;
constexpr uint32_t kFlyKeySpace = 44;
constexpr uint32_t kFlyKeyShift = 225;

struct FlyCam {
    float eye[3]{0.0f, 2.0f, 8.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float speed = 4.0f;
};

inline void flyCamForward(const FlyCam& cam, float& x, float& y, float& z) {
    const float cp = std::cos(cam.pitch);
    x = std::sin(cam.yaw) * cp;
    y = std::sin(cam.pitch);
    z = -std::cos(cam.yaw) * cp;
}

inline void flyCamUpdate(FlyCam& cam, const InputState& input, float dt) {
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if ((input.mouseActive & 2u) != 0) {
        // Один кадр не разворачивает взгляд больше чем на ~55°. Прыжок курсора иначе ставит «вперёд» назад.
        float dx = input.deltaX;
        float dy = input.deltaY;
        constexpr float kMax = 240.0f;
        if (dx > kMax) {
            dx = kMax;
        }
        if (dx < -kMax) {
            dx = -kMax;
        }
        if (dy > kMax) {
            dy = kMax;
        }
        if (dy < -kMax) {
            dy = -kMax;
        }
        cam.yaw += dx * 0.004f;
        cam.pitch -= dy * 0.004f;
    }
    constexpr float kLimit = 1.553343f;
    if (cam.pitch > kLimit) {
        cam.pitch = kLimit;
    }
    if (cam.pitch < -kLimit) {
        cam.pitch = -kLimit;
    }
    if (input.wheelDelta != 0.0f) {
        cam.speed *= std::exp(input.wheelDelta * 0.12f);
        if (cam.speed < 0.25f) {
            cam.speed = 0.25f;
        }
        if (cam.speed > 80.0f) {
            cam.speed = 80.0f;
        }
    }
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = 0.0f;
    flyCamForward(cam, fx, fy, fz);
    const float rx = -fz;
    const float rz = fx;
    float mx = 0.0f;
    float my = 0.0f;
    float mz = 0.0f;
    if (inputKeyDown(input, kFlyKeyW)) {
        mx += fx;
        my += fy;
        mz += fz;
    }
    if (inputKeyDown(input, kFlyKeyS)) {
        mx -= fx;
        my -= fy;
        mz -= fz;
    }
    if (inputKeyDown(input, kFlyKeyD)) {
        mx += rx;
        mz += rz;
    }
    if (inputKeyDown(input, kFlyKeyA)) {
        mx -= rx;
        mz -= rz;
    }
    if (inputKeyDown(input, kFlyKeySpace) || inputKeyDown(input, kFlyKeyE)) {
        my += 1.0f;
    }
    if (inputKeyDown(input, kFlyKeyShift) || inputKeyDown(input, kFlyKeyQ)) {
        my -= 1.0f;
    }
    const float len = std::sqrt(mx * mx + my * my + mz * mz);
    if (len > 1.0e-6f) {
        const float s = cam.speed * dt / len;
        cam.eye[0] += mx * s;
        cam.eye[1] += my * s;
        cam.eye[2] += mz * s;
    }
}

inline void flyCamTarget(const FlyCam& cam, float target[3]) {
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = 0.0f;
    flyCamForward(cam, fx, fy, fz);
    target[0] = cam.eye[0] + fx;
    target[1] = cam.eye[1] + fy;
    target[2] = cam.eye[2] + fz;
}

} // namespace burnhope
