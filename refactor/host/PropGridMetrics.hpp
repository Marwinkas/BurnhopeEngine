#pragma once

#include "ui/Types.hpp"

namespace burnhope {

// The sizes a property row builder still computes with, read from the "prop_row", "vector_field" and
// "dropdown_trigger" classes of assets/ui/styles.json. Everything else about a row (label column, value slot,
// axis caps, hair line, swatch, check box) lives in the blueprints prop_row / vec_row3 / vec_row2 and their classes.
// Row builders read it once and pass it down, so nothing looks a class up while a row is laid out or drawn.
struct PropMetrics {
    float slotH = 0.0f;
    float fieldW = 0.0f;
    float hoverAlpha = 0.0f;
    float menuChrome = 0.0f;
    float vecPadL = 0.0f;
    float vecPadR = 0.0f;
};

[[nodiscard]] PropMetrics propMetrics(const UiState& s);

// A number field takes the whole value slot, so scalar and vector rows end on the same right edge.
void propFillWidth(UiState& s, uint16_t id);

// Click handler of the vector row's lock, reset square and axis caps; `user` is the UiState and `id` the clicked node.
void propVecClick(void* user, uint16_t id);

} // namespace burnhope
