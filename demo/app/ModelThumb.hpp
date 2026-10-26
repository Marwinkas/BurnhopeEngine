#pragma once

#include <cstdint>

namespace burnhope {

// Renders the model at `path` (3/4 view, z-buffered, flat shaded) into 64 x 64 cell `cell` of the 512 px wide RGBA atlas.
// False when the model cannot be read. The cell stays transparent around the silhouette.
[[nodiscard]] bool modelThumbBake(uint8_t* atlas, uint8_t cell, const char* path);

} // namespace burnhope
