#pragma once

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Moves keyboard focus and keeps the UiFocused tag on exactly the focused entity (state.focus).
// Focusable = UiFocusable or UiEditable. Left press focuses the nearest focusable at/above state.under (blurs when
// there is none); Tab / Shift+Tab walk focusables in draw order and wrap; Escape blurs; a focus whose entity died or
// became hidden is dropped. Any change resets state.caretTick.
void interactFocus(flecs::world& world, const InputFrame& in, GuiInteractState& state);

// Sets focus explicitly (id 0 blurs).
void guiSetFocus(flecs::world& world, GuiInteractState& state, uint64_t id);

} // namespace burnhope::gui
