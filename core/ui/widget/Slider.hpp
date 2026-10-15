#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiRange {
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.0f;
};

struct UiValueHook {
    void (*fn)(void* user, float value) = nullptr;
    void* user = nullptr;
};

using UiValueChangeFn = void (*)(void* user, float value);

void dragSlider(UiState& s, uint16_t id, float x);
void dragSpin(UiState& s, uint16_t id, float x);
uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange = nullptr, void* user = nullptr, float width = 0.0f);
void uiBindSlider(UiState& s, const char* name, float value, UiValueChangeFn onChange, void* user);
void uiSliderSet(UiState& s, uint16_t id, float value);
uint16_t uiNumber(UiState& s, uint16_t parent, float value, float minV, float maxV, float step, UiValueChangeFn onChange = nullptr, void* user = nullptr);
uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step);
bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV);
bool uiSliderInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);
bool uiSliderDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
