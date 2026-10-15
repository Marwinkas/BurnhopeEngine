#pragma once

#include <cstdint>

namespace burnhope {

struct UiState;

// Наведение — только подкраска узла, помеченного UiHoverable.
void uiHover(UiState& s, uint16_t id);
void uiHoverTint(const UiState& s, uint16_t id, float& r, float& g, float& b, float& a);

} // namespace burnhope
