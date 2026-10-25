#pragma once

#include "ui/widget/Range.hpp"
#include "platform/Input.hpp"
#include "ui/Interaction.hpp"
#include "ui/Types.hpp"

#include <cstddef>

namespace burnhope {

void uiWriteNumber(char* dst, std::size_t cap, float value);
void uiRangeSet(UiState& s, uint16_t id, float value);
bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV);
void dragSpin(UiState& s, uint16_t id, float x);
uint16_t uiNumber(UiState& s, uint16_t parent, float value, float minV, float maxV, float step, UiValueChangeFn onChange = nullptr, void* user = nullptr, float width = 0.0f, float height = 0.0f, float shrink = 0.0f);
void uiNumberFill(UiState& s, uint16_t id, bool showFillProgress);
uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step);
void uiNumberWatch(UiState& s);
void uiNumberBind(UiState& s);
bool uiNumberInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope
