#pragma once

#include "Str.hpp"
#include "ui/State.hpp"

namespace burnhope {

struct UiScrollMetrics {
    float contentH = 0;
    float maxOff = 0;
    float thumbH = 0;
    float travel = 0;
    float t = 0;
    uint8_t shown = 0;
};

UiScrollMetrics uiCalcScrollMetrics(float viewH, float contentH, float offset);

inline constexpr float kAccent[3][3] = {
    {0.36f, 0.55f, 0.92f},
    {0.45f, 0.72f, 0.48f},
    {0.86f, 0.55f, 0.32f},
};

} // namespace burnhope
