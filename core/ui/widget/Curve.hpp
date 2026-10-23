#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiCurve {
    float x[4]{0.0f, 0.25f, 0.75f, 1.0f};
    float y[4]{0.0f, 0.25f, 0.75f, 1.0f};
};

uint16_t uiCurve(UiState& s, uint16_t parent);
bool uiCurveDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
