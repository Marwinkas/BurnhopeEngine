#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiTip(UiState& s, uint16_t parent);
void uiTipAt(UiState& s, uint16_t id, const char* text, float x, float y);

} // namespace burnhope
