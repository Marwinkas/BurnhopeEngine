#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiRange {
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.0f;
    uint8_t fill = 1;
};

struct UiValueHook {
    void (*fn)(void* user, float value) = nullptr;
    void* user = nullptr;
};

using UiValueChangeFn = void (*)(void* user, float value);

void uiRangeSet(UiState& s, uint16_t id, float value);
bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV);
void dragSpin(UiState& s, uint16_t id, float x);
uint16_t uiNumber(UiState& s, uint16_t parent, float value, float minV, float maxV, float step, UiValueChangeFn onChange = nullptr, void* user = nullptr);
void uiNumberFill(UiState& s, uint16_t id, bool showFillProgress);
uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step);
void uiNumberWatch(UiState& s);
bool uiNumberInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);
bool uiNumberDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
