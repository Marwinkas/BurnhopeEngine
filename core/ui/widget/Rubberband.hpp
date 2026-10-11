#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiRubber {
    uint16_t overlay = kUiNone;
    uint8_t active = 0;
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    void (*pick)(void* user, uint16_t child) = nullptr;
    void* user = nullptr;
};

uint16_t uiRubberband(UiState& s, uint16_t host, void (*pick)(void* user, uint16_t child), void* user);
bool uiRubberbandTrack(UiState& s, uint16_t host, const InputFrame& in);

} // namespace burnhope
