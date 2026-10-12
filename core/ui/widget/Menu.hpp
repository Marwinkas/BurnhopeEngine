#pragma once

#include "ui/State.hpp"

#include <span>

namespace burnhope {

inline constexpr uint8_t kUiMenuCap = 32;

struct UiMenuPop {
    uint16_t row[kUiMenuCap]{};
    void (*fn[kUiMenuCap])(void*){};
    void* user = nullptr;
    uint8_t n = 0;
};

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
    bool separator = false;
    bool disabled = false;
};

void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user);
void uiMenuClose(UiState& s);
uint16_t uiMakeMenu(UiState& s);
void uiMenuStyle(UiState& s, float width, float itemH, float pad, float gap);
void uiMenuShowAt(UiState& s, uint16_t menuId, float x, float y);

} // namespace burnhope
