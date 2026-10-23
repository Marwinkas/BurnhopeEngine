#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiTitleBar {};

inline constexpr uint8_t kBinSlotChrome = 10;

uint16_t uiWindowChrome(UiState& s, uint16_t parent);
void uiInstallChromeBin(UiState& s);

} // namespace burnhope
