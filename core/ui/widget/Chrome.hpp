#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiWindowChrome(UiState& s, uint16_t parent);
void uiMarkTab(UiState& s, uint16_t strip, uint16_t tab);
void uiInstallChromeBin(UiState& s);

} // namespace burnhope
