#pragma once

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Pixels of travel before a scrub (UiDraggable.mode 2) latches.
constexpr float kGuiScrubDeadzone = 3.0f;
// Scrub sensitivity: fraction of the (maxV - minV) range per pixel; Shift scales it by kGuiScrubFine.
constexpr float kGuiScrubPerPixel = 0.005f;
constexpr float kGuiScrubFine = 0.1f;

// The single drag engine (slider, splitter, scrollbar thumb, colour plane, resize...).
// Press on a UiDraggable (nearest at/above state.under) captures the pointer into state.capture and writes UiDragState.
//   mode 0 absolute: value = (pointer - box.origin) / box.size along the axis, clamped to [minV, maxV]
//   mode 1 relative: value = startValue + (pointer - origin) / box.size
//   mode 2 scrub:    once |travel| >= kGuiScrubDeadzone latches, value += pointer.dx * sensitivity (UiDragState.pad0 = latched)
// axis 2 drives value from x and valueY from y. Escape restores the start values; release pushes kGuiActionDragEnd.
// UiDragState.dx/dy hold the travel (mode 0/1) or the accumulated scrub delta (mode 2).
void interactDrag(flecs::world& world, const InputFrame& in, GuiInteractState& state);

} // namespace burnhope::gui
