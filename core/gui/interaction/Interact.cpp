#include "gui/interaction/Interact.hpp"

#include "gui/interaction/InteractClick.hpp"
#include "gui/interaction/InteractDrag.hpp"
#include "gui/interaction/InteractFocus.hpp"
#include "gui/interaction/InteractHover.hpp"
#include "gui/interaction/InteractScroll.hpp"

namespace burnhope::gui {

void guiInteractRun(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style,
                    const GuiClipboard* clip) {
    state.actionCount = 0;
    interactHover(world, in, state, style);
    interactFocus(world, in, state);
    interactClick(world, in, state);
    interactDrag(world, in, state);
    interactScroll(world, in, state);
    interactTextEdit(world, in, state, style, clip);
}

} // namespace burnhope::gui
