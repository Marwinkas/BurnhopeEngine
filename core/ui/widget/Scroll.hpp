#pragma once

#include "ui/State.hpp"

namespace burnhope {

void uiBindScroll(UiState& s, uint16_t id, uint16_t inner);
bool scrollThumb(const UiState& s, uint16_t id, UiBox& thumb);
void dragScroll(UiState& s, uint16_t id, float y);
void scrollBy(UiState& s, uint16_t id, float wheel);
void scrollClamp(UiState& s, uint16_t id);
void scrollClampAll(UiState& s);
uint16_t scrollOwner(const UiState& s, uint16_t id);
void uiScrollOverlay(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap);
bool uiScrollInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
