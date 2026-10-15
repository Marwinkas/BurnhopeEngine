#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiCurve {
    float x[4]{0.0f, 0.25f, 0.75f, 1.0f};
    float y[4]{0.0f, 0.25f, 0.75f, 1.0f};
};

struct UiGradient {
    uint8_t stops = 2;
    float t[4]{0.0f, 1.0f, 1.0f, 1.0f};
    float r[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float g[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float b[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

uint16_t uiCurve(UiState& s, uint16_t parent);
uint16_t uiGradient(UiState& s, uint16_t parent);
bool uiCurveDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
