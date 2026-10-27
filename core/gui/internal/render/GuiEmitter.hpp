#pragma once

#include <cstdint>

#include "data/GuiComponents.hpp"
#include "interaction/InteractState.hpp"
#include "style/GuiStyleSheet.hpp"
#include "rhi/UiPrimitive.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiEmitDepthCap = 48; // deepest entity nesting that is painted
constexpr uint32_t kGuiEmitRootCap = 32;  // UiRoot entities painted per frame
constexpr uint32_t kGuiEmitLineCap = 8;   // wrapped text lines per entity
constexpr float kGuiDragFillAlpha = 0.55f; // accent fill of a horizontal UiDraggable up to its value

// Paints every visible entity into `out` (legacy slot-48 UiPrimitive layout, 80 bytes) and returns the primitive count
// (never above `cap`). Zero allocation.
//
// Order: UiRoot entities that are not overlays first (creation order), then overlay roots by ascending UiOverlay::layer.
// Inside a root entities are visited depth first in child order: a parent is painted before its children and an earlier
// sibling before a later one. Child order is the flecs OrderedChildren order when the parent has that trait (the loader
// and the virtual pool add it), otherwise the table order of ecs children().
//
// Per entity (skipped with its subtree when it has UiHidden; skipped alone when its UiClip is empty):
//   1. fill rect: palette(UiPaint::token) * UiPaint::tint, rounded by UiPaint::radius; nothing when the alpha is 0 or when
//      a photo is set on a token-0 entity (sprite only);
//      token 0 (kTokCustom) is white, so the tint carries custom colors and tint[3] = 0 makes an entity transparent;
//   2. photo sprite when UiPaint::photo != kGuiNoPhoto (texId = photo slot, tinted);
//   2b. horizontal UiDraggable (axis 0): accent bar from the left edge to value * width, under the border and text;
//   3. border stroke (palette(borderToken), UiPaint::borderW) when borderW > 0.5;
//   4. UiText glyphs in palette kTokText inside the box minus its Yoga padding. Lines wrap (guiTextWrap) only when the
//      padded box can hold at least 1.5 lines, otherwise one line; align 0/1/2 = left/center/right; a single line is
//      centered vertically, wrapped blocks are top aligned.
//   5. UiEditable: selection highlight, and the caret while the entity has UiFocused and `interact` is non-null (blink
//      from interact->caretTick: visible during the first 30 of every 60 frames). Editable text starts at box.x + kGuiEditPadX - scrollX
//      (InteractTextEdit.hpp) and is never wrapped.
// Every primitive carries the entity's UiClip in the legacy clip fields (extra[1..3] + pad0 bits; corner[] for photos).
uint32_t guiEmit(flecs::world& world, const GuiStyleSheet& sheet, UiPrimitive* out, uint32_t cap,
                 const GuiInteractState* interact = nullptr);

} // namespace burnhope::gui
