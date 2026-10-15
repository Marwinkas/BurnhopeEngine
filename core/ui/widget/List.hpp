#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiFoldState {
    float openH = 0.0f;
};

void uiFold(UiState& s, uint16_t id);
uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text);

struct UiPropGrid {
    float labelW = 140.0f;
    uint16_t grip = kUiNone;
    uint16_t labels = kUiNone;
    uint16_t values = kUiNone;
};

uint16_t uiPropHost(UiState& s, uint16_t parent);
uint16_t uiPropRow(UiState& s, uint16_t host, const char* label);
uint16_t uiPropCheck(UiState& s, uint16_t host, const char* label, uint8_t kind = 0, uint8_t group = 0, bool on = false);
uint16_t uiPropNumber(UiState& s, uint16_t host, const char* label, float value, float minV, float maxV, float step);
uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu);

} // namespace burnhope
