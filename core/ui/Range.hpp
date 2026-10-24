#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

struct UiRange {
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.0f;
    uint8_t fill = 1;
};

struct UiValueHook {
    void (*fn)(void* user, float value) = nullptr;
    void* user = nullptr;
};

using UiValueChangeFn = void (*)(void* user, float value);

static_assert(std::is_trivially_copyable_v<UiRange>);
static_assert(std::is_trivially_copyable_v<UiValueHook>);

} // namespace burnhope
