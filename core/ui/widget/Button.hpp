#pragma once

#include "ui/Interaction.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiIconChrome {};

struct UiState;

struct UiButtonConfig {
    const char* label = nullptr;
    float width = 0.0f;
    float height = 0.0f;
    float padX = -1.0f;
    bool fillWidth = false;  // Явный флаг 100% ширины
    bool fillHeight = false; // Явный флаг 100% высоты
    uint8_t iconSlot = 255;
    UiClickFn onClick = nullptr;
    void* user = nullptr; 
};


uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg = {});
uint16_t uiIconBtn(UiState& s, uint16_t parent, uint8_t icon, float side, uint16_t* glyph = nullptr);
uint16_t uiSlotBtn(UiState& s, uint16_t parent, uint8_t slot, float side, uint16_t* icon = nullptr);
void uiButtonTintIcon(UiState& s, uint16_t icon, bool on);
uint16_t uiButtonIconChild(const UiState& s, uint16_t id);

void uiButtonBin(UiState& s);
void uiButtonMark(UiState& s, uint16_t id, bool on);

} // namespace burnhope
