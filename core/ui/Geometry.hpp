#pragma once

#include "ui/Types.hpp"

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr float kUiUnset = -1.0f;

struct UiBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct UiBounds {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct UiScissor {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool enabled = false;
};

inline bool hitBox(const UiBox& b, float x, float y) noexcept {
    return b.w > 0.5f && b.h > 0.5f && x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h;
}

static_assert(std::is_trivially_copyable_v<UiBox>);
static_assert(std::is_trivially_copyable_v<UiBounds>);
static_assert(std::is_trivially_copyable_v<UiScissor>);

} // namespace burnhope