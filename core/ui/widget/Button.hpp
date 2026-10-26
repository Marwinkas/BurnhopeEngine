#pragma once

#include "ui/Icon.hpp"
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
    UiIcon icon = UiIcon::None; // procedural glyph drawn by the button itself
    bool ghost = false;         // chromeless: no plate, text-coloured glyph
    UiClickFn onClick = nullptr;
    void* user = nullptr; 
};


uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg = {});
// Active tools read as accent glyphs; `ghost` strips the plate so only the glyph is drawn.
// Gives a toolbar button the shared tool frame (transparent plate, 1px border, 4px corners).
void uiToolFrame(UiState& s, uint16_t id);
void uiToolButtonState(UiState& s, uint16_t id, bool active, bool ghost = true);
// Width of `text` in the button font plus `extraPad`.
float uiButtonTextWidth(const UiState& s, const char* text, float extraPad);
// Sizes the button to exactly fit `text` (+ extraPad) and returns that width.
float uiButtonFitText(UiState& s, uint16_t id, const char* text, float extraPad);
uint16_t uiIconBtn(UiState& s, uint16_t parent, uint8_t icon, float side, uint16_t* glyph = nullptr);
uint16_t uiSlotBtn(UiState& s, uint16_t parent, uint8_t slot, float side, uint16_t* icon = nullptr);
void uiButtonTintIcon(UiState& s, uint16_t icon, bool on);
uint16_t uiButtonIconChild(const UiState& s, uint16_t id);

void uiButtonBin(UiState& s);
void uiButtonMark(UiState& s, uint16_t id, bool on);

} // namespace burnhope
