#pragma once

#include <cstdint>

namespace burnhope::gui {

constexpr float kGuiRowIndentPx = 12.0f; // blank space per tree depth level
constexpr float kGuiRowCellPx = 20.0f;   // advance of an inline icon / the chevron column
constexpr uint8_t kGuiRowTree = 1;       // GuiRow::flags: reserve the chevron column (leaf rows stay aligned with branches)

// Typed layout fields of one list / tree row, drawn around the row's UiText (the name). Plain rows leave it zeroed.
//   depth    tree nesting level, indented by kGuiRowIndentPx each
//   chevron  icon cell of the disclosure glyph (0 = none); the column exists when kGuiRowTree is set
//   icon     icon cell drawn before the name (0 = none)
//   col1     second column (date) at 60% of the row; col2 third column (size), right-aligned from 84%
struct GuiRow {
    char col1[24] = {};
    char col2[16] = {};
    uint8_t depth = 0;
    uint8_t icon = 0;
    uint8_t chevron = 0;
    uint8_t flags = 0;
    uint8_t pad[4] = {};
};
static_assert(sizeof(GuiRow) % 16 == 0);

[[nodiscard]] inline bool guiRowActive(const GuiRow& r) {
    return r.depth != 0 || r.icon != 0 || r.chevron != 0 || r.flags != 0 || r.col1[0] != '\0' || r.col2[0] != '\0';
}

} // namespace burnhope::gui
