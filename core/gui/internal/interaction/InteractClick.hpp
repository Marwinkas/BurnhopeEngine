#pragma once

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Left press on a UiClickable (nearest at/above state.under) tags it UiPressed; releasing over the same entity pushes
// {entity, UiClickable.action}. A press that turned into a drag gesture (capture == press) never fires.
// Requires interactHover to have filled state.under this frame.
void interactClick(flecs::world& world, const InputFrame& in, GuiInteractState& state);

} // namespace burnhope::gui
