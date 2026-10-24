#pragma once

#include "ui/Command.hpp"
#include "ui/widget/BinSlots.hpp"

#include <span>

namespace burnhope {

void uiMenuBin(UiState& s);

struct UiMenuBar {};
struct UiMenuSlide {};

struct UiCommandTarget {};
struct UiMenuOpen {};

void uiSetMenu(UiState& s, uint16_t id);
uint16_t uiMenu(const UiState& s);

void uiMenuSetShown(UiState& s, bool shown);
bool uiMenuShown(const UiState& s);

struct UiMenuItem {
    const char* label = nullptr;
    uint8_t iconSlot = 255;
    UiCommandFn onClick = nullptr;
    bool separator = false;
    bool disabled = false;
};

void uiMenuRowBind(UiState& s, uint16_t row, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command);
uint16_t uiMenuRow(UiState& s, uint16_t menu, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command);
void uiMarkMenuBar(UiState& s, uint16_t id);
void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user, float above = -1.0f);
void uiMenuClose(UiState& s);
uint16_t uiMakeMenu(UiState& s);
void uiMenuStyle(UiState& s, float width, float itemH, float pad, float gap);
void uiMenuShowAt(UiState& s, uint16_t menuId, float x, float y, float above = -1.0f);
uint16_t uiMenuButton(UiState& s, uint16_t parent, const char* label, uint16_t menu);
void uiMenuCaption(UiState& s, uint16_t button, const char* text);
bool uiMenuInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope
