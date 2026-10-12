#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiValueHook {
    void (*fn)(void* user, float value) = nullptr;
    void* user = nullptr;
};

using UiValueChangeFn = void (*)(void* user, float value);

void dragSlider(UiState& s, uint16_t id, float x);
void dragSpin(UiState& s, uint16_t id, float x);
uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange = nullptr, void* user = nullptr, float width = 0.0f);
uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step);
bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV);
bool uiSliderInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope
