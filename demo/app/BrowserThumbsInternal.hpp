#pragma once

#include <cstdint>

#include "app/BrowserModel.hpp"
#include "app/BrowserThumbs.hpp"

namespace burnhope {

// Pixel work of the thumbnail atlas (BrowserThumbBake.cpp); BrowserThumbs.cpp decides which cell shows what.
[[nodiscard]] uint64_t thumbHashPath(const char* path);
// The cell filled with the theme plate colour.
void thumbFillPlate(ThumbCache& c, uint32_t cell);
// Draws the icon of `kind` over the plate; false when its SVG cannot be rasterized.
[[nodiscard]] bool thumbBakeKindIcon(ThumbCache& c, uint32_t cell, FileKind kind);
// Decodes the image file at `path` into the cell.
[[nodiscard]] bool thumbBakeTexture(ThumbCache& c, uint32_t cell, const char* path);
// Shades the .bhmat at `path` on a sphere into the cell.
[[nodiscard]] bool thumbBakeMaterial(ThumbCache& c, uint32_t cell, const char* path);
// Copies a 64 x 64 RGBA block into the cell.
void thumbCopyCell(ThumbCache& c, uint32_t cell, const uint8_t* rgba);

} // namespace burnhope
