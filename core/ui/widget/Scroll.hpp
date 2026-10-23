#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiScroll {
    float offset = 0.0f;
    uint16_t inner = kUiNone;
    float (*contentHeight)(const UiState& s, uint16_t id) = nullptr;
    void (*onScroll)(UiState& s, uint16_t id) = nullptr;
    uint8_t follow = 1;
};

struct UiScrollMetrics {
    float contentH = 0;
    float maxOff = 0;
    float thumbH = 0;
    float travel = 0;
    float t = 0;
    uint8_t shown = 0;
};

UiScrollMetrics uiCalcScrollMetrics(float viewH, float contentH, float offset);

inline constexpr uint8_t kBinSlotScroll = 7;
void uiScrollBin(UiState& s);

void uiBindScroll(UiState& s, uint16_t id, uint16_t inner);
bool scrollThumb(const UiState& s, uint16_t id, UiBox& thumb);
void dragScroll(UiState& s, uint16_t id, float y);
void scrollBy(UiState& s, uint16_t id, float wheel);
void scrollReset(UiState& s, uint16_t id);
void scrollClamp(UiState& s, uint16_t id);
void scrollClampAll(UiState& s);
uint16_t scrollOwner(const UiState& s, uint16_t id);
void uiScrollOverlay(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap);
bool uiScrollInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
