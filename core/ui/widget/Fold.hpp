#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiTree {
    uint16_t body = kUiNone;
    uint16_t clip = kUiNone;
    uint8_t open = 0;
    uint8_t pad = 0;
    float full = 0.0f;
};

uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text);
uint16_t uiFoldBody(UiState& s, uint16_t parent, const char* title);
void uiFold(UiState& s, uint16_t id);

} // namespace burnhope
