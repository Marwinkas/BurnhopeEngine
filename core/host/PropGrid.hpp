#pragma once

#include "ui/Types.hpp"

namespace burnhope {

struct UiRowHover {};

struct UiPropLabel {
    uint16_t next = kUiNone;
};

struct UiPropRoll {
    uint16_t labels = kUiNone;
    uint16_t sections = kUiNone;
    uint16_t sectionTail = kUiNone;
    uint16_t bar = kUiNone;
};

struct UiPropGrid {
    float labelW = 140.0f;
    uint16_t grip = kUiNone;
    uint16_t labels = kUiNone;
    uint16_t values = kUiNone;
};

uint16_t uiPropHost(UiState& s, uint16_t parent);
uint16_t uiPropRow(UiState& s, uint16_t host, const char* label);
uint16_t uiPropCheck(UiState& s, uint16_t host, const char* label, uint8_t kind = 0, uint8_t group = 0, bool on = false);
uint16_t uiPropNumber(UiState& s, uint16_t host, const char* label, float value, float minV, float maxV, float step);
uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu);

struct UiPropVec3 {
    uint16_t x = kUiNone;
    uint16_t y = kUiNone;
    uint16_t z = kUiNone;
    uint16_t lock = kUiNone;
    uint16_t reset = kUiNone;
};

struct UiPropVec2 {
    uint16_t x = kUiNone;
    uint16_t y = kUiNone;
};

struct UiVecCtl {
    uint16_t field[3]{};
    float home[3]{};
    uint8_t which = 0;
    uint8_t linked = 0;
};

[[nodiscard]] bool uiPropLinked(const UiState& s, uint16_t lock);

struct UiPropSection {
    uint16_t next = kUiNone;
    uint8_t cat = 0;
    uint8_t owner = 0;
    uint8_t live = 1;
    uint8_t pad = 0;
};

struct UiPropFilterBar {
    uint16_t which = kUiNone;
    uint16_t chips = kUiNone;
    uint16_t search = kUiNone;
};

struct UiPropFilterChip {
    uint16_t section = kUiNone;
    uint8_t all = 0;
    uint8_t live = 0;
};

UiPropVec3 uiPropVec3(UiState& s, uint16_t host, const char* label, float x, float y, float z, float minV, float maxV, float step);
UiPropVec2 uiPropVec2(UiState& s, uint16_t host, const char* label, float x, float y, float minV, float maxV, float step);
uint16_t uiPropSwatch(UiState& s, uint16_t host, const char* label, float r, float g, float b);
uint16_t uiPropSection(UiState& s, uint16_t parent, const char* title, uint8_t cat, uint8_t owner);
uint16_t uiPropFilterBar(UiState& s, uint16_t parent);
void uiPropShowMask(UiState& s, uint8_t mask);

} // namespace burnhope
