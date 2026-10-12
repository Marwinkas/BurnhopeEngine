#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t splitTarget(const UiState& s, uint16_t split);
void splitDrag(UiState& s, uint16_t split, float x, float y);
bool splitAlive(const UiState& s, uint16_t split);
uint8_t splitCursor(const UiState& s, uint16_t split);
bool splitVertical(const UiState& s, uint16_t split);
UiBox splitGrip(const UiState& s, uint16_t split);
void splitSync(UiState& s);
bool uiSplitInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
