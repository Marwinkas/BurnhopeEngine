#pragma once

#include "data/GuiComponents.hpp"
#include "interaction/InteractState.hpp"
#include "style/GuiStyleSheet.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Resolves the pointer target (state.under / state.hover), maintains the UiHovered tag and writes the hover/press
// brightness into UiPaint.tint of UiHoverable entities (rgb = 1 + a hovered, 1 - a pressed, a = guiStyleHoverAlpha).
// While a drag owns the pointer the captured entity stays hovered.
void interactHover(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style);

} // namespace burnhope::gui
