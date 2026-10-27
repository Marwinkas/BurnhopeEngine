#pragma once

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "gui/interaction/InteractTextEdit.hpp"
#include "gui/style/GuiStyleSheet.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// One interaction frame: clears state.actions, then hit/hover -> focus -> click -> drag -> scroll -> text edit.
// UiBox/UiClip must be current (layout + scissor of the previous frame). `clip` may be null. No allocation.
void guiInteractRun(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style,
                    const GuiClipboard* clip);

} // namespace burnhope::gui
