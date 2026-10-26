#pragma once

#include "ui/Types.hpp"

namespace burnhope {

// Every size a property row, vector row or swatch needs, read from the "prop_row", "vector_row", "vector_field",
// "vector_lock" and "swatch" classes of assets/ui/styles.json.
// Row builders read it once and pass it down, so nothing looks a class up while a row is laid out or drawn.
struct PropMetrics {
    float rowH = 0.0f;
    float slotH = 0.0f;
    float padV = 0.0f;
    float margin = 0.0f;
    float labelW = 0.0f;
    float labelPad = 0.0f;
    float gap = 0.0f;
    float fieldW = 0.0f;
    float trailW = 0.0f;
    float hoverAlpha = 0.0f;
    float controlMax = 0.0f;
    float vectorMax = 0.0f;
    float lockGap = 0.0f;
    float lockSide = 0.0f;
    float badgeW = 0.0f;
    float badgeGain = 0.0f;
    float vecH = 0.0f;
    float vecRadius = 0.0f;
    float vecFieldMin = 0.0f;
    float vecPadL = 0.0f;
    float vecPadR = 0.0f;
    float swatchBorder = 0.0f;
    float checkSide = 0.0f;
    float menuChrome = 0.0f;
    float hairH = 0.0f;
    float hairAlpha = 0.0f;
};

[[nodiscard]] PropMetrics propMetrics(const UiState& s);

// A number field takes the whole value slot, so scalar and vector rows end on the same right edge.
void propFillWidth(UiState& s, uint16_t id);

// Click handler of the vector row's lock, reset square and axis caps; `user` is the UiState and `id` the clicked node.
void propVecClick(void* user, uint16_t id);

} // namespace burnhope
