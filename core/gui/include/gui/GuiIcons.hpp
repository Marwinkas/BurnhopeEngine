#pragma once

#include <cstdint>
#include <string_view>

namespace burnhope::gui {

// Icon atlas contract shared by the loader (names), the emitter (cells) and the host (rasterizing the SVGs).
// Cell 0 is "no icon"; cell n sits at column n % kGuiIconCells, row n / kGuiIconCells of a kGuiIconEdge square atlas.
constexpr uint32_t kGuiIconCells = 8;
constexpr uint32_t kGuiIconCell = 32;
constexpr uint32_t kGuiIconEdge = kGuiIconCells * kGuiIconCell;
constexpr float kGuiIconDrawPx = 16.0f; // side of the glyph a node draws inside its box
constexpr float kGuiIconPad = 4.0f;      // padding between an icon-only button's edge and its glyph
constexpr float kGuiIconMaxPx = 24.0f;    // largest glyph an icon-only button draws
constexpr float kGuiIconTrailPad = 6.0f; // gap between a trailing glyph (icon next to a caption) and the right edge

// Cell of a registered icon name ("play", "chevron_down"), 0 when the name is unknown.
[[nodiscard]] uint8_t guiIconFind(std::string_view name);
// Asset path of the SVG behind a cell; nullptr for cell 0 and cells past the table.
[[nodiscard]] const char* guiIconPath(uint32_t cell);
// Number of registered cells including cell 0.
[[nodiscard]] uint32_t guiIconCount();

} // namespace burnhope::gui
