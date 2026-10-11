#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiVirtualGrid(UiState& s, uint16_t parent, const UiVirtualGridConfig& cfg);
void uiVirtualGridRefresh(UiState& s, uint16_t gridId);

} // namespace burnhope
