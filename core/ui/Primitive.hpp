#pragma once

#include "rhi/DescriptorSlots.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace burnhope {

// Слоты дескрипторной кучи Vulkan 1.4
constexpr uint32_t kUiDescFont = 661;
constexpr uint32_t kUiDescPhoto = 662;
constexpr uint32_t kUiDescPrimsSlot = kUiDescPrims;   // 48
constexpr uint8_t kPhotoSlots = 8;

constexpr uint32_t kUiPrimCap = 2048;
constexpr uint32_t kUiHeaderBytes = 64;
constexpr uint32_t kUiPrimBytes = 80;
constexpr uint32_t kUiFontBase = kUiHeaderBytes + kUiPrimCap * kUiPrimBytes;

enum class UiPrimFlag : uint32_t {
    None      = 0,
    Glyph     = 1 << 0,
    Clip      = 1 << 1,
    Icon      = 1 << 2,
    Wheel     = 1 << 3,
    Ramp      = 1 << 4,
    Photo     = 1 << 5,
    Stroke    = 1 << 6,
    Shadow    = 1 << 7,
    Ribbon    = 1 << 8,
    Transform = 1 << 9,
};

inline constexpr UiPrimFlag operator|(UiPrimFlag a, UiPrimFlag b) noexcept {
    return static_cast<UiPrimFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr bool operator&(UiPrimFlag a, UiPrimFlag b) noexcept {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

constexpr uint32_t kUiFlagGlyph     = static_cast<uint32_t>(UiPrimFlag::Glyph);
constexpr uint32_t kUiFlagClip      = static_cast<uint32_t>(UiPrimFlag::Clip);
constexpr uint32_t kUiFlagIcon      = static_cast<uint32_t>(UiPrimFlag::Icon);
constexpr uint32_t kUiFlagWheel     = static_cast<uint32_t>(UiPrimFlag::Wheel);
constexpr uint32_t kUiFlagRamp      = static_cast<uint32_t>(UiPrimFlag::Ramp);
constexpr uint32_t kUiFlagPhoto     = static_cast<uint32_t>(UiPrimFlag::Photo);
constexpr uint32_t kUiFlagStroke    = static_cast<uint32_t>(UiPrimFlag::Stroke);
constexpr uint32_t kUiFlagShadow    = static_cast<uint32_t>(UiPrimFlag::Shadow);
constexpr uint32_t kUiFlagRibbon    = static_cast<uint32_t>(UiPrimFlag::Ribbon);
constexpr uint32_t kUiFlagTransform = static_cast<uint32_t>(UiPrimFlag::Transform);

// std430: Ровно 80 байт. Extra начинается со смещения 48, Corner со смещения 64
struct UiPrimitive {
    float posX = 0.0f;
    float posY = 0.0f;
    float sizeX = 0.0f;
    float sizeY = 0.0f;
    float color[4]{};
    uint32_t texId = 0;
    uint32_t flags = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    float extra[4]{};
    float corner[4]{};
};

static_assert(std::is_trivially_copyable_v<UiPrimitive>);
static_assert(sizeof(UiPrimitive) == kUiPrimBytes);
static_assert(offsetof(UiPrimitive, extra) == 48);
static_assert(offsetof(UiPrimitive, corner) == 64);

constexpr float kUiAngleLo = -180.0f;
constexpr float kUiAngleHi = 180.0f;
constexpr float kUiScaleLo = 0.0f;
constexpr float kUiScaleHi = 4.0f;
constexpr float kUiFilterLo = 0.0f;
constexpr float kUiFilterHi = 2.0f;

inline uint16_t uiPackU16(float v, float lo, float hi) noexcept {
    const float t = hi > lo ? std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    return static_cast<uint16_t>(t * 65535.0f + 0.5f);
}
inline float uiUnpackU16(uint16_t v, float lo, float hi) noexcept {
    return lo + (static_cast<float>(v) / 65535.0f) * (hi - lo);
}
inline uint8_t uiPackU8(float v, float lo, float hi) noexcept {
    const float t = hi > lo ? std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    return static_cast<uint8_t>(t * 255.0f + 0.5f);
}
inline float uiUnpackU8(uint8_t v, float lo, float hi) noexcept {
    return lo + (static_cast<float>(v) / 255.0f) * (hi - lo);
}

inline void uiPackTransform(uint32_t& pad0, uint32_t& pad1,
                            float angle, float scaleX, float scaleY,
                            float bright, float contrast) noexcept {
    const uint16_t a = uiPackU16(angle, kUiAngleLo, kUiAngleHi);
    const uint16_t sx = uiPackU16(scaleX, kUiScaleLo, kUiScaleHi);
    const uint16_t sy = uiPackU16(scaleY, kUiScaleLo, kUiScaleHi);
    const uint8_t br = uiPackU8(bright, kUiFilterLo, kUiFilterHi);
    const uint8_t ct = uiPackU8(contrast, kUiFilterLo, kUiFilterHi);
    pad0 = static_cast<uint32_t>(a) | (static_cast<uint32_t>(sx) << 16);
    pad1 = static_cast<uint32_t>(sy) | (static_cast<uint32_t>(br) << 16) | (static_cast<uint32_t>(ct) << 24);
}

inline void uiUnpackTransform(uint32_t pad0, uint32_t pad1,
                              float& angle, float& scaleX, float& scaleY,
                              float& bright, float& contrast) noexcept {
    angle = uiUnpackU16(static_cast<uint16_t>(pad0 & 0xFFFFu), kUiAngleLo, kUiAngleHi);
    scaleX = uiUnpackU16(static_cast<uint16_t>(pad0 >> 16), kUiScaleLo, kUiScaleHi);
    scaleY = uiUnpackU16(static_cast<uint16_t>(pad1 & 0xFFFFu), kUiScaleLo, kUiScaleHi);
    bright = uiUnpackU8(static_cast<uint8_t>((pad1 >> 16) & 0xFFu), kUiFilterLo, kUiFilterHi);
    contrast = uiUnpackU8(static_cast<uint8_t>((pad1 >> 24) & 0xFFu), kUiFilterLo, kUiFilterHi);
}

inline float uiPhotoLod(float blur, uint32_t mips) noexcept {
    const float t = std::clamp(blur, 0.0f, 1.0f);
    const float span = mips > 1 ? static_cast<float>(mips - 1) : 0.0f;
    return t * span;
}

} // namespace burnhope