#pragma once

#include "input/Input.hpp"

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace burnhope {

enum class PropType : uint8_t {
    FloatSlider = 0,
    IntInput,
    ToggleBool,
    ColorPicker,
    ReadOnlyInt,
    Vec3Slider,
};

struct PropertyBinding {
    uint32_t nameHash = 0;
    PropType type = PropType::FloatSlider;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    void* dataPtr = nullptr;
};

static_assert(std::is_trivially_copyable_v<PropertyBinding>);

inline float inspectorClamp(float value, float lo, float hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

inline bool inspectorHandleInput(PropertyBinding* props, uint32_t count, const InputState& input) {
    if (props == nullptr) {
        return false;
    }
    bool changed = false;
    const bool click = (input.mousePressed & 1u) != 0;
    const bool drag = (input.mouseActive & 1u) != 0 && input.deltaX != 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        PropertyBinding& prop = props[i];
        if (prop.dataPtr == nullptr || prop.type == PropType::ReadOnlyInt) {
            continue;
        }
        if (prop.type == PropType::ToggleBool && click) {
            auto* flag = static_cast<uint8_t*>(prop.dataPtr);
            *flag = *flag == 0 ? 1 : 0;
            changed = true;
            continue;
        }
        if (!drag) {
            continue;
        }
        const float span = prop.maxVal - prop.minVal;
        const float step = input.deltaX * span / 200.0f;
        if (prop.type == PropType::FloatSlider) {
            auto* value = static_cast<float*>(prop.dataPtr);
            *value = inspectorClamp(*value + step, prop.minVal, prop.maxVal);
            changed = true;
        } else if (prop.type == PropType::Vec3Slider) {
            auto* value = static_cast<float*>(prop.dataPtr);
            value[0] = inspectorClamp(value[0] + step, prop.minVal, prop.maxVal);
            changed = true;
        } else if (prop.type == PropType::IntInput) {
            auto* value = static_cast<int32_t*>(prop.dataPtr);
            *value += static_cast<int32_t>(step);
            changed = true;
        }
    }
    return changed;
}

inline uint32_t inspectorWriteLines(const PropertyBinding* props, uint32_t count, char* dst, uint32_t cap) {
    if (props == nullptr || dst == nullptr || cap == 0) {
        return 0;
    }
    uint32_t used = 0;
    dst[0] = '\0';
    for (uint32_t i = 0; i < count; ++i) {
        const PropertyBinding& prop = props[i];
        char row[64]{};
        int wrote = 0;
        if (prop.type == PropType::FloatSlider && prop.dataPtr != nullptr) {
            wrote = std::snprintf(row, sizeof(row), "%08x %.3f\n", prop.nameHash, *static_cast<const float*>(prop.dataPtr));
        } else if (prop.type == PropType::ToggleBool && prop.dataPtr != nullptr) {
            wrote = std::snprintf(row, sizeof(row), "%08x %u\n", prop.nameHash, static_cast<unsigned>(*static_cast<const uint8_t*>(prop.dataPtr)));
        } else {
            wrote = std::snprintf(row, sizeof(row), "%08x\n", prop.nameHash);
        }
        if (wrote < 0) {
            continue;
        }
        for (int c = 0; c < wrote && used + 1 < cap; ++c) {
            dst[used] = row[c];
            used += 1;
        }
    }
    if (used < cap) {
        dst[used] = '\0';
    }
    return used;
}

} // namespace burnhope
