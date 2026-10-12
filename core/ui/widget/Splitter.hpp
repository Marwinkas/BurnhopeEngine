#pragma once

#include "ui/State.hpp"

namespace burnhope {

void splitDrag(UiState& s, uint16_t split, float x, float y);
bool splitAlive(const UiState& s, uint16_t split);
uint8_t splitCursor(const UiState& s, uint16_t split);
bool splitVertical(const UiState& s, uint16_t split);
UiBox splitGrip(const UiState& s, uint16_t split);
void splitSync(UiState& s);

} // namespace burnhope
