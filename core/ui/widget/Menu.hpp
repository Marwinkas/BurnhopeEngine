#pragma once

#include "ui/Command.hpp"

#include <span>

namespace burnhope {

inline constexpr uint8_t kBinSlotMenu = 13;
inline constexpr uint8_t kBinSlotChoice = 19;
void uiMenuBin(UiState& s);

struct UiMenuBar {};
struct UiMenuSlide {};

struct UiMenuItem {
    const char* label = nullptr;
    uint8_t iconSlot = 255;
    UiCommandFn onClick = nullptr;
    bool separator = false;
    bool disabled = false;
};

void uiMarkMenuBar(UiState& s, uint16_t id);
void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user, float above = -1.0f);
void uiMenuClose(UiState& s);
uint16_t uiMakeMenu(UiState& s);
void uiMenuStyle(UiState& s, float width, float itemH, float pad, float gap);
void uiMenuShowAt(UiState& s, uint16_t menuId, float x, float y, float above = -1.0f);
uint16_t uiMenuButton(UiState& s, uint16_t parent, const char* label, uint16_t menu);

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

uint16_t uiChoice(UiState& s, uint16_t parent, const UiChoiceConfig& cfg);
void uiBindChoice(UiState& s, const char* name, uint8_t selected, void (*onPick)(void* user, uint8_t index), void* user);
void uiMenuCaption(UiState& s, uint16_t button, const char* text);
float uiChoiceWidth(const UiState& s, const char* const* labels, uint8_t count);
void uiChoiceFit(UiState& s, uint16_t button, float width);
bool uiMenuInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
