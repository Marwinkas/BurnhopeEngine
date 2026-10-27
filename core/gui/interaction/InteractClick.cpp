#include "gui/interaction/InteractClick.hpp"

#include "gui/input/GuiHitTest.hpp"

namespace burnhope::gui {

void interactClick(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    if (state.press != 0 && guiEntityOf(world, state.press).id() == 0) {
        state.press = 0;
    }
    if ((in.pointer.pressed & kPointerLeft) != 0 && state.capture == 0) {
        const flecs::entity target = guiAncestorWith<UiClickable>(guiEntityOf(world, state.under));
        if (target.id() != 0) {
            state.press = target.id();
            target.add<UiPressed>();
        }
    }
    if ((in.pointer.released & kPointerLeft) != 0 && state.press != 0) {
        const flecs::entity pressed = guiEntityOf(world, state.press);
        const uint64_t id = state.press;
        state.press = 0;
        if (pressed.id() != 0) {
            pressed.remove<UiPressed>();
            const flecs::entity over = guiAncestorWith<UiClickable>(guiEntityOf(world, state.under));
            const UiClickable* click = pressed.try_get<UiClickable>();
            if (click != nullptr && over.id() == id && state.capture != id) {
                guiPushAction(state, id, click->action);
            }
        }
    }
}

} // namespace burnhope::gui
