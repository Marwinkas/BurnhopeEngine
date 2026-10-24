#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiPrimitive;

struct ColorBook {
    float r = 0.36f;
    float g = 0.55f;
    float b = 0.92f;
    float a = 1.0f;
    float h = 0.62f;
    float s = 0.61f;
    float v = 0.92f;
    uint8_t useAlpha = 1;
    uint8_t mode = 0;
    uint8_t present = 0;
    uint8_t drop = 0;
    uint32_t gen = 0;
};

struct UiColorPickerComponent {
    ColorBook local{};
    uint16_t colorPanel = kUiNone;
    uint16_t colorSwatch = kUiNone;
    uint16_t colorTarget = kUiNone;
    uint16_t colorWheel = kUiNone;
    uint16_t colorVal = kUiNone;
    uint16_t colorAlpha = kUiNone;
    uint16_t colorHex = kUiNone;
    uint16_t colorRgb[3]{kUiNone, kUiNone, kUiNone};
    uint16_t colorHsv[3]{kUiNone, kUiNone, kUiNone};
    uint16_t colorNum[7]{kUiNone, kUiNone, kUiNone, kUiNone, kUiNone, kUiNone, kUiNone};
    uint16_t colorAnchor = kUiNone;
    uint16_t colorAlphaBtn = kUiNone;
    uint16_t colorMenuBtn = kUiNone;
    uint16_t colorWinBtn = kUiNone;
    uint16_t colorPickBtn = kUiNone;
    ColorBook* book = nullptr;
    void (*onColor)(void* user, UiState& state, uint16_t anchor, float r, float g, float b) = nullptr;
    void* colorUser = nullptr;
    uint32_t colorSeen = 0;
    uint8_t colorApplied = 0;
    uint8_t colorHost = 0;
};

inline ColorBook& colorInk(UiColorPickerComponent& picker) {
    return picker.book != nullptr ? *picker.book : picker.local;
}

inline const ColorBook& colorInk(const UiColorPickerComponent& picker) {
    return picker.book != nullptr ? *picker.book : picker.local;
}

inline UiColorPickerComponent* uiColorFind(UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (!s.ent[i].is_alive() || !s.ent[i].has<UiColorPickerComponent>()) {
            continue;
        }
        return s.ent[i].try_get_mut<UiColorPickerComponent>();
    }
    return nullptr;
}

inline const UiColorPickerComponent* uiColorFind(const UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (!s.ent[i].is_alive() || !s.ent[i].has<UiColorPickerComponent>()) {
            continue;
        }
        return s.ent[i].try_get<UiColorPickerComponent>();
    }
    return nullptr;
}

inline UiColorPickerComponent* uiColor(UiState& s) {
    return uiColorFind(s);
}

inline const UiColorPickerComponent* uiColor(const UiState& s) {
    return uiColorFind(s);
}

void uiInstallColorBin(UiState& s);

void uiSetColorBook(UiState& s, ColorBook* book);
void uiEnsureColorPopup(UiState& s);
void uiBuildColor(UiState& s);
void colorOnOpen(void* user, uint16_t id);
void colorOnValue(void* user, uint16_t id, float value);

} // namespace burnhope
