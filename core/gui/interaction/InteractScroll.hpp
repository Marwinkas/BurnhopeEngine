#pragma once

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Pixels scrolled per wheel notch.
constexpr float kGuiWheelStep = 48.0f;

// Clamps every UiScroll to [0, content - view], then routes the wheel (Shift+wheel scrolls horizontally) to the
// nearest UiScrollable at/above state.under that can still move on the wheel's axis. Ignored during a drag.
void interactScroll(flecs::world& world, const InputFrame& in, GuiInteractState& state);

} // namespace burnhope::gui
