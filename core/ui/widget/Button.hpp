#pragma once

#include "ui/Model.hpp"

namespace burnhope {

struct UiState;

struct UiButtonConfig {
    const char* label = nullptr;
    float width = 0.0f;
    float height = 28.0f;
    uint8_t iconSlot = 255;
    UiClickFn onClick = nullptr;
    void* user = nullptr;
};

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg);
uint16_t uiButton(UiState& s, uint16_t parent, const char* label, UiClickFn fn, void* user);
uint16_t uiIconButton(UiState& s, uint16_t parent, uint8_t iconSlot, UiClickFn fn, void* user);
uint16_t uiCheck(UiState& s, uint16_t parent, const char* text, uint8_t kind = 0, uint8_t group = 0, bool on = false);
void uiBindCheck(UiState& s, uint16_t id, uint8_t kind, uint8_t group, bool on);
int uiCheckOn(const UiState& s, const char* name);

} // namespace burnhope
