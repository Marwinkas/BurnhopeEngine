#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

uint16_t uiLabel(UiState& s, uint16_t parent, const char* text);

void uiLabelBin(UiState& s);

} // namespace burnhope
