#pragma once

#include "ui/State.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <flecs.h>

namespace burnhope {

// File browser host. Lives as a Flecs singleton, not on UiState.
struct UiFileBrowser {
    char fileDir[192]{};
    char fileName[16][40]{};
    uint8_t fileIsDir[16]{};
    uint8_t fileCount = 0;
    uint8_t filePick = 0xff;
    uint16_t fileRows[16]{};
    uint16_t filePanel = kUiNone;
    uint16_t fileHead = kUiNone;
    uint16_t fileTarget = kUiNone;
    uint16_t photoView = kUiNone;
    uint16_t fileView = kUiNone;
    uint16_t fileFrame = kUiNone;
    void* files = nullptr;
};

struct UiCalendarComponent {
    int calY = 2026;
    int calM = 10;
    int calSel = 0;
    uint16_t calPanel = kUiNone;
    uint16_t calHead = kUiNone;
    uint16_t calDay[42]{};
    uint16_t calTarget = kUiNone;
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
    ColorBook* book = nullptr;
    uint32_t colorSeen = 0;
    uint8_t colorUseAlpha = 1;
    uint8_t colorMode = 0;
    uint8_t colorPresent = 0;
    uint8_t colorApplied = 0;
    uint8_t colorHost = 0;
    uint8_t colorDrop = 0;
};

struct UiDock {
    uint16_t home = kUiNone;
    uint16_t panel = kUiNone;
};

struct UiHostNote {
    bool tool = false;
    bool color = false;
    bool file = false;
};

struct UiPick {
    uint16_t id = kUiNone;
    char name[32]{};
    uint8_t len = 0;
};

inline UiFileBrowser& uiFiles(UiState& s) {
    return s.world.ensure<UiFileBrowser>();
}
inline const UiFileBrowser& uiFiles(const UiState& s) {
    return *s.world.try_get<UiFileBrowser>();
}
inline UiCalendarComponent& uiCal(UiState& s) {
    return s.world.ensure<UiCalendarComponent>();
}
inline const UiCalendarComponent& uiCal(const UiState& s) {
    return *s.world.try_get<UiCalendarComponent>();
}
inline UiColorPickerComponent& uiColor(UiState& s) {
    return s.world.ensure<UiColorPickerComponent>();
}
inline const UiColorPickerComponent& uiColor(const UiState& s) {
    return *s.world.try_get<UiColorPickerComponent>();
}
inline UiDock& uiDock(UiState& s) {
    return s.world.ensure<UiDock>();
}
inline UiHostNote& uiHostNote(UiState& s) {
    return s.world.ensure<UiHostNote>();
}
inline UiPick& uiPick(UiState& s) {
    return s.world.ensure<UiPick>();
}

} // namespace burnhope
