#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiCurve(UiState& s, uint16_t parent);
uint16_t uiGradient(UiState& s, uint16_t parent);
bool uiCurveDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
