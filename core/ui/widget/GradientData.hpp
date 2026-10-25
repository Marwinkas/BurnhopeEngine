#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

struct UiGradient {
    uint8_t stops = 2;
    float t[4]{0.0f, 1.0f, 1.0f, 1.0f};
    float r[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float g[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float b[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

static_assert(std::is_trivially_copyable_v<UiGradient>);

} // namespace burnhope
