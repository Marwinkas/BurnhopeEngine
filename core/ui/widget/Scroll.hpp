#pragma once

#include "ui/State.hpp"

namespace burnhope {

void uiBindScroll(UiState& s, uint16_t id, uint16_t inner);
bool scrollThumb(const UiState& s, uint16_t id, UiBox& thumb);
void dragScroll(UiState& s, uint16_t id, float y);

} // namespace burnhope
