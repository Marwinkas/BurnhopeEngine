#pragma once

#include "ui/Model.hpp"

namespace burnhope {

struct UiState;

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg);
uint16_t uiButton(UiState& s, uint16_t parent, const char* label, UiClickFn fn, void* user);
uint16_t uiIconButton(UiState& s, uint16_t parent, uint8_t iconSlot, UiClickFn fn, void* user);

} // namespace burnhope
