#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiRubberband(UiState& s, uint16_t host, void (*pick)(void* user, uint16_t child), void* user);
bool uiRubberbandTrack(UiState& s, uint16_t host, const InputFrame& in);

} // namespace burnhope
