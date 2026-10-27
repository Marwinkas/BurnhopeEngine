#pragma once

#include <string_view>

#include "gui/data/GuiComponents.hpp"
#include "gui/style/GuiStyleSheet.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiLoadDepthCap = 24; // deepest nesting of "children"
constexpr uint32_t kGuiToggleCap = 128;   // "toggles" links per screen
constexpr uint32_t kGuiTogglePathCap = 64;

// Spawns a widget tree from JSON and parents it under `parent` (an invalid entity = a new UiRoot). Zero-copy parse via
// core/io/JsonCursor.hpp; keys may appear in any order, unknown keys are skipped.
//
// Node object:
//   "class" | "type"  style class name (guiStyleFind); an unknown name leaves the node unstyled
//   "name"            entity name (skipped when a sibling already uses it)
//   "text"            UiText (UTF-8, truncated to 127 bytes)       "size": px   "align": "left"|"center"|"right"
//   "color": [r,g,b,a]  UiPaint token 0 with this tint (without it a token-0 node is transparent)
//   "photo": slot     UiPaint::photo                                "hidden": true  starts hidden (UiHidden + Yoga display none)
//   "behaviors": ["hover","click","drag","scroll","focus","edit","virtual","overlay"]
//        hover   UiHoverable                     click   UiClickable{"action"}
//        drag    UiDraggable{"axis": "x"|"y"|"xy", "mode", "value", "minV", "maxV", "flat": no value bar} + UiDragState
//        scroll  UiScrollable + UiScroll + UiClipChildren
//        focus   UiFocusable                     edit    UiEditable{"maxLen","multiline"} + UiFocusable + UiHoverable
//        virtual UiVirtual{"count","itemW","itemH","columns","itemAction","itemClass"} + scroll components
//        overlay UiOverlay{"placement","layer","dismiss"} + UiRoot + UiHidden (closed)
//   "toggles": "path"  UiToggle: a click on this node shows/hides (or opens/closes an overlay) the node at `path`, a
//                      "::"-separated name path below the screen root; an unresolved path fails the load
//   "children": [ node, ... ]
// Every spawned entity gets UiBox, UiPaint, UiText, UiFlex (guiLayoutAttach), the flecs OrderedChildren trait, and the
// style class applied after the Yoga node exists. On success *rootOut (when non-null) receives the top entity.
// On a syntax error, a missing parent Yoga node or a too deep tree the partial tree is destroyed, *rootOut is cleared
// and false is returned.
bool guiLoadScreen(flecs::world& world, const GuiStyleSheet& sheet, std::string_view json, flecs::entity parent,
                   flecs::entity* rootOut);

} // namespace burnhope::gui
