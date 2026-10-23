#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiLabel(UiState& s, uint16_t parent, const char* text);

inline constexpr uint8_t kBinSlotLabel = 2;
void uiLabelBin(UiState& s);

} // namespace burnhope
