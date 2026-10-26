#pragma once

#include "ui/Types.hpp"

namespace burnhope {

struct UiSegment {
    uint8_t selected = 0;
    void (*onPick)(void* user, uint8_t index) = nullptr;
    void* user = nullptr;
};

struct UiSegmentItem {
    uint16_t strip = kUiNone;
    uint8_t index = 0;
};

struct UiSegmentConfig {
    const char* const* labels = nullptr;
    uint8_t count = 0;
    uint8_t selected = 0;
    float itemW = 56.0f;
    float itemH = 24.0f;
    void (*onPick)(void* user, uint8_t index) = nullptr;
    void* user = nullptr;
};

// A row of mutually exclusive buttons; the picked one reads as active. Returns the row.
uint16_t uiSegment(UiState& s, uint16_t parent, const UiSegmentConfig& cfg);
// Moves the highlight to `index` without calling onPick.
void uiSegmentSelect(UiState& s, uint16_t strip, uint8_t index);

} // namespace burnhope
