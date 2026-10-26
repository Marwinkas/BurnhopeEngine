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
// Current value of a range control, or `fallback` when `id` holds none.
[[nodiscard]] float uiRangeVal(const UiState& s, uint16_t id, float fallback);
void dragSpin(UiState& s, uint16_t id, float x);
uint16_t uiNumber(UiState& s, uint16_t parent, float value, float minV, float maxV, float step, UiValueChangeFn onChange = nullptr, void* user = nullptr);
void uiNumberFill(UiState& s, uint16_t id, bool showFillProgress);
// Calls `onChange` with every value the user gives the slider or number `id`.
void uiBindValue(UiState& s, uint16_t id, UiValueChangeFn onChange, void* user);
void uiNumberWatch(UiState& s);
void uiNumberBind(UiState& s);
bool uiNumberInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope
