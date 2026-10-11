#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h);
void uiSetIcon(UiState& s, uint16_t id, uint8_t slot);

} // namespace burnhope
