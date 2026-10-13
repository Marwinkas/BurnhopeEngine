#pragma once

#include "ui/State.hpp"

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
    float colorR = 0.36f;
    float colorG = 0.55f;
    float colorB = 0.92f;
    float colorH = 0.62f;
    float colorS = 0.61f;
    float colorV = 0.92f;
    float colorA = 1.0f;
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
    uint32_t colorSeen = 0;
    uint8_t colorUseAlpha = 1;
    uint8_t colorMode = 0;
    uint8_t colorPresent = 0;
    uint8_t colorApplied = 0;
    uint8_t colorHost = 0;
    uint8_t colorDrop = 0;
};

inline UiColorPickerComponent& uiColor(UiState& s) {
    return s.world.ensure<UiColorPickerComponent>();
}
void uiInstallColorBin(UiState& s);

inline const UiColorPickerComponent& uiColor(const UiState& s) {
    return *s.world.try_get<UiColorPickerComponent>();
}

void uiSetColorBook(UiState& s, ColorBook* book);
void uiBuildColor(UiState& s);
void colorOnOpen(void* user, uint16_t id);
void colorOnValue(void* user, uint16_t id, float value);
bool colorCustomDraw(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
