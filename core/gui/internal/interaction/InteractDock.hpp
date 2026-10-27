#pragma once

#include "data/GuiComponents.hpp"
#include "interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Tab headers of a UiDock. A left press on a tab selects its panel and arms a drag; after kGuiDragDeadzone px of travel the
// tab follows the pointer (move cursor, accent outline on the tab bar it would join) and the release over a tab bar moves
// the tab, together with its panel, into that bar at the pointer's position. A press and release on a tab's close button
// closes the panel. Escape cancels a live drag. State lives in GuiInteractState (tabPress / tabPhase / tabBar).
void interactDock(flecs::world& world, const InputFrame& in, GuiInteractState& state);

} // namespace burnhope::gui
