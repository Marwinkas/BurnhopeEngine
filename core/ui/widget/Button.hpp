#pragma once

#include "ui/Icon.hpp"
#include "ui/Interaction.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiState;

// A button owns its label, glyph, photo and click; everything about its size, padding and look is its style class.
struct UiButtonConfig {
    const char* cls = nullptr;  // styles.json class: btn_default (null), ghost_button, tool_button, ...
    const char* label = nullptr;
    uint8_t iconSlot = 255;     // photo slot drawn inset in the button, sized by the class's "icon"
    UiIcon icon = UiIcon::None; // glyph drawn by the button itself
    UiClickFn onClick = nullptr;
    void* user = nullptr;
};

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg = {});
uint16_t uiButtonIconChild(const UiState& s, uint16_t id);
// Width of `text` in the button font plus `extraPad`, and a node sized to exactly that. Menu triggers still size
// themselves this way; they go when the menu moves to a blueprint.
float uiButtonTextWidth(const UiState& s, const char* text, float extraPad);
float uiButtonFitText(UiState& s, uint16_t id, const char* text, float extraPad);

void uiButtonBin(UiState& s);

} // namespace burnhope
