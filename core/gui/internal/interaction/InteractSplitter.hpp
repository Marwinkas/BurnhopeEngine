#pragma once

#include "data/GuiComponents.hpp"
#include "interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Window cursor kinds the interaction stages request (Window.cpp windowSetCursor).
constexpr uint8_t kGuiCursorEastWest = 1;
constexpr uint8_t kGuiCursorNorthSouth = 2;
constexpr uint8_t kGuiCursorMove = 8;

// The sibling a splitter resizes (the one before or after it in the Yoga child order); an id-0 entity when there is none.
flecs::entity guiSplitterTarget(flecs::world& world, flecs::entity splitter);

// Hovers and drags splitter grips: a press on a UiSplitter captures the pointer (state.split) and every frame until the
// button is released the resized sibling gets width (vertical bar) or height (horizontal bar) = size at the press +- the
// pointer travel, clamped to UiSplitter::minSize..maxSize. Sets state.cursor while the grip is hovered or held.
void interactSplitter(flecs::world& world, const InputFrame& in, GuiInteractState& state);

// Hides every splitter whose resized sibling is hidden and shows it again with the sibling (a closed dock leaves no grip).
void guiSplitterRefresh(flecs::world& world);

} // namespace burnhope::gui
