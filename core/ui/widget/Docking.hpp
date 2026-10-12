#pragma once

#include "ui/State.hpp"

namespace burnhope {

void uiDockTo(UiState& s, uint8_t edge);

struct ChromePointer {
    uint8_t cursor = 0;
    uint8_t took = 0;
};

ChromePointer uiChromePointer(UiState& s, const InputFrame& in, uint16_t hit, bool press, bool down, bool release);
void uiInstallDockOverlay(UiState& s);

} // namespace burnhope
