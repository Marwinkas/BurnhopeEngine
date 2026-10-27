#pragma once

#include <cstdint>

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiOverlayCap = 32; // overlay entities handled per frame

// Places and dismisses overlays (menus, tooltips, dropdowns). An overlay is an entity with UiOverlay + UiRoot + UiFlex.
//
// Must run BEFORE guiLayoutRun in the frame: the position is written as an absolute Yoga offset (left/top) on the
// overlay's node, and layout turns it into the UiBox. Layout must therefore keep the Yoga position of UiRoot entities
// that also have UiOverlay. The size used for fitting is the overlay's previous UiBox (its Yoga style size on the first
// frame), so a freshly opened overlay settles within one extra frame.
//
// Per overlay:
//   open == 0: UiHidden is added. open != 0: UiHidden is removed and the overlay is positioned next to the anchor rect
//   (anchorX/Y/W/H) by `placement` (0 below, 1 above, 2 right, 3 left, 4 at the point anchor; when placement is 4 and the
//   whole anchor is zero the pointer position is used). When the preferred side does not fit it flips to the opposite side
//   if that fits better; the result is then clamped inside [0, screen].
// Light dismiss (before placement): on a pointer press, an open overlay with dismissOnOutside is closed when the pointer
// is outside its box and outside its anchor rect and no overlay of a higher layer contains the pointer (clicking inside
// a submenu keeps its parents open, clicking in a parent closes the submenus above it). Escape closes the topmost
// dismissible overlay. state.overlayOpen receives the number of open overlays after the pass.
void guiOverlayRun(flecs::world& world, const InputFrame& in, GuiInteractState& state, float screenW, float screenH);

} // namespace burnhope::gui
