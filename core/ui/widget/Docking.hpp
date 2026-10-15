#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiResizable {};

struct UiDock {
    uint16_t home = kUiNone;
    uint16_t panel = kUiNone;
    uint8_t edges = 15;
    uint8_t edge = 0;
    uint8_t hintOn = 0;
    uint8_t edgeOn = 0;
    UiBox hint{};
    UiBox edgeLine{};
};

inline UiDock& uiDock(UiState& s) {
    return s.world.ensure<UiDock>();
}

void uiDockTo(UiState& s, uint8_t edge);

struct ChromePointer {
    uint8_t cursor = 0;
    uint8_t took = 0;
};

ChromePointer uiChromePointer(UiState& s, const InputFrame& in, uint16_t hit, bool press, bool down, bool release);
void uiInstallDockOverlay(UiState& s);
bool uiDockInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
