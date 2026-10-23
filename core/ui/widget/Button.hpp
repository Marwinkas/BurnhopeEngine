#pragma once

#include "ui/Interaction.hpp"
#include "ui/Primitive.hpp"

namespace burnhope {

struct UiIconChrome {};

struct UiState;
struct UiPrimitive;

struct UiCheck {
    uint8_t on = 0;
    uint8_t kind = 0;
    uint8_t group = 0;
};

struct UiButtonConfig {
    const char* label = nullptr;
    float width = 0.0f;
    float height = 0.0f;
    bool fillWidth = false;  // Явный флаг 100% ширины
    bool fillHeight = false; // Явный флаг 100% высоты
    uint8_t iconSlot = 255;
    UiClickFn onClick = nullptr;
    void* user = nullptr; 
};


void uiSetDisabled(UiState& s, uint16_t id, bool disabled);
uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg = {});
uint16_t uiIconBtn(UiState& s, uint16_t parent, uint8_t icon, float side, uint16_t* glyph = nullptr);
uint16_t uiSlotBtn(UiState& s, uint16_t parent, uint8_t slot, float side, uint16_t* icon = nullptr);
void uiButtonTintIcon(UiState& s, uint16_t icon, bool on);
uint16_t uiButtonIconChild(const UiState& s, uint16_t id);
uint16_t uiCheck(UiState& s, uint16_t parent, const char* text, uint8_t kind = 0, uint8_t group = 0, bool on = false);
void uiBindCheck(UiState& s, uint16_t id, uint8_t kind, uint8_t group, bool on);
bool uiCheckDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);
void uiCheckPress(UiState& s, uint16_t id);
int uiCheckOn(const UiState& s, const char* name);
int uiCheckOn(const UiState& s, uint16_t id);

inline constexpr uint8_t kBinSlotButton = 1;
inline constexpr uint8_t kBinSlotCheck = 4;
inline constexpr uint8_t kBinSlotClose = 6;
void uiButtonBin(UiState& s);

} // namespace burnhope
