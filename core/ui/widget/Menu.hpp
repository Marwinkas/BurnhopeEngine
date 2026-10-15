#pragma once

#include "ui/State.hpp"

#include <span>

namespace burnhope {

struct UiMenuBar {};

struct UiMenuCmd {
    uint8_t index = 0;
};

inline constexpr uint8_t kUiMenuCap = 32;

struct UiMenuMetrics {
    uint16_t opened = kUiNone;
    float w = 180.0f;
    float itemH = 28.0f;
    float pad = 6.0f;
    float gap = 4.0f;
};

inline UiMenuMetrics& uiMenuMetrics(UiState& s) {
    return s.world.ensure<UiMenuMetrics>();
}

struct UiMenuItem {
    const char* label = nullptr;
    uint8_t iconSlot = 255;
    void (*onClick)(void* user) = nullptr;
    const UiMenuItem* children = nullptr;
    uint8_t childCount = 0;
    bool separator = false;
    bool disabled = false;
};

struct UiMenuPop {
    uint16_t row[kUiMenuCap]{};
    void (*fn[kUiMenuCap])(void*){};
    const UiMenuItem* item[kUiMenuCap]{};
    void* user = nullptr;
    uint8_t n = 0;
    uint8_t hover = 255;
    uint16_t sub = kUiNone;
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
bool uiMenuInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
