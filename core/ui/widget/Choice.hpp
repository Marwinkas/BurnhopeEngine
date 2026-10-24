#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiChoice {
    uint16_t menu = kUiNone;
    uint16_t caption = kUiNone;
    uint8_t index = 0;
    void (*onPick)(void* user, uint8_t index) = nullptr;
    void* user = nullptr;
};

struct UiChoiceConfig {
    const char* const* labels = nullptr;
    uint8_t count = 0;
    uint8_t selected = 0;
    float width = 140.0f;
    void (*onPick)(void* user, uint8_t index) = nullptr;
    void* user = nullptr;
};

void uiChoiceTake(UiState& s, uint16_t owner, uint16_t row, uint16_t item);
uint16_t uiChoice(UiState& s, uint16_t parent, const UiChoiceConfig& cfg);
void uiBindChoice(UiState& s, const char* name, uint8_t selected, void (*onPick)(void* user, uint8_t index), void* user);
float uiChoiceWidth(const UiState& s, const char* const* labels, uint8_t count);
void uiChoiceFit(UiState& s, uint16_t button, float width);
void uiChoiceBind(UiState& s);

} // namespace burnhope
