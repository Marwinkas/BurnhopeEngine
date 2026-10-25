#pragma once

#include "ui/Range.hpp"
#include "platform/Input.hpp"
#include "ui/Interaction.hpp"
#include "ui/Types.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiProgress {};

void uiSliderBin(UiState& s);

void dragSlider(UiState& s, uint16_t id, float x);
uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange = nullptr, void* user = nullptr, float width = 0.0f);
void uiBindSlider(UiState& s, const char* name, float value, UiValueChangeFn onChange, void* user);
void uiSliderSet(UiState& s, uint16_t id, float value);
bool uiSliderInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope
