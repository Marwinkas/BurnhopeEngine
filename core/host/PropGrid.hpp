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
    float labelW = 140.0f;
    uint8_t seeded = 0; // labelW took the prop_row class width
};

// Tag on the right-hand slot of a property row; reset buttons sit in it.
struct UiPropTrail {};

// Tag on a section's column grip; every grip follows UiPropRoll::labelW.
struct UiPropGrip {};

struct UiPropGrid {
    float labelW = 140.0f;
    uint16_t grip = kUiNone;
    uint16_t labels = kUiNone;
    uint16_t values = kUiNone;
};

uint16_t uiPropHost(UiState& s, uint16_t parent);
// Adds the label/value column grip inside a section body; every section grip shares one label width.
uint16_t uiPropGrip(UiState& s, uint16_t body);
// Hides the column grip of a section body that lays out its own cards (the material page).
void uiPropNoGrip(UiState& s, uint16_t body);
// The trailing slot of the property row that holds `node`, or kUiNone when `node` is in no property row.
[[nodiscard]] uint16_t uiPropTrail(const UiState& s, uint16_t node);
uint16_t uiPropRow(UiState& s, uint16_t host, const char* label);
uint16_t uiPropCheck(UiState& s, uint16_t host, const char* label, uint8_t kind = 0, uint8_t group = 0, bool on = false);
uint16_t uiPropNumber(UiState& s, uint16_t host, const char* label, float value, float minV, float maxV, float step);
// The value slot a control built by uiPropCheck / uiPropNumber / uiPropMenu sits in, and the row that holds a value slot.
[[nodiscard]] uint16_t uiPropSlotOf(const UiState& s, uint16_t control);
[[nodiscard]] uint16_t uiPropRowOf(const UiState& s, uint16_t valueSlot);
// Gives the row of `valueSlot` a fixed height `rowH` and the slot itself `innerH` (children may overflow it).
// Does nothing when both already match.
void uiPropRowHeight(UiState& s, uint16_t valueSlot, float rowH, float innerH);
// Lets the value slot of a row grow to `maxW` px (rows with a thumbnail need more than the usual control width).
void uiPropValueMax(UiState& s, uint16_t valueSlot, float maxW);
void uiPropRowShow(UiState& s, uint16_t valueSlot, bool on);
uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu);

struct UiPropVec3 {
    uint16_t x = kUiNone;
    uint16_t y = kUiNone;
    uint16_t z = kUiNone;
    uint16_t lock = kUiNone;
    uint16_t reset = kUiNone;
    uint16_t row = kUiNone;
};

struct UiPropVec2 {
    uint16_t x = kUiNone;
    uint16_t y = kUiNone;
    uint16_t row = kUiNone;
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
