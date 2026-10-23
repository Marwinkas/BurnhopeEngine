#pragma once

#include "ui/widget/Number.hpp"

namespace burnhope {

inline constexpr uint8_t kBinSlotSlider = 3;
inline constexpr uint8_t kBinSlotProgress = 9;
void uiSliderBin(UiState& s);

void dragSlider(UiState& s, uint16_t id, float x);
uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange = nullptr, void* user = nullptr, float width = 0.0f);
void uiBindSlider(UiState& s, const char* name, float value, UiValueChangeFn onChange, void* user);
void uiSliderSet(UiState& s, uint16_t id, float value);
bool uiSliderInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);
bool uiSliderDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope
