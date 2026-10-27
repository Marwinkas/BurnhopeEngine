#include "interaction/Interact.hpp"

#include "interaction/InteractClick.hpp"
#include "interaction/InteractDrag.hpp"
#include "interaction/InteractFocus.hpp"
#include "interaction/InteractHover.hpp"
#include "interaction/InteractScroll.hpp"

namespace burnhope::gui {

void guiInteractRun(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style,
                    const GuiClipboard* clip) {
    state.actionCount = 0;
    interactHover(world, in, state, style);
    // A grabbed scrollbar thumb owns the left button: the stages below see it released.
    InputFrame rest = in;
    if (interactScrollBar(world, in, state)) {
        rest.pointer.pressed &= ~kPointerLeft;
        rest.pointer.released &= ~kPointerLeft;
        rest.pointer.down &= ~kPointerLeft;
        rest.clicks = 0;
    }
    interactFocus(world, rest, state);
    interactClick(world, rest, state);
    interactDrag(world, rest, state);
    interactScroll(world, rest, state);
    interactTextEdit(world, rest, state, style, clip);
}

} // namespace burnhope::gui
