#include "gui/interaction/InteractClick.hpp"

#include "gui/input/GuiHitTest.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"

namespace burnhope::gui {
namespace {

void applyToggle(flecs::world& world, flecs::entity trigger, const UiToggle& toggle) {
    const flecs::entity target = guiEntityOf(world, toggle.target);
    if (target.id() == 0) {
        return;
    }
    if (UiOverlay* overlay = target.try_get_mut<UiOverlay>()) {
        overlay->open = overlay->open != 0 ? 0 : 1;
        if (overlay->open != 0) {
            if (const UiBox* box = trigger.try_get<UiBox>()) {
                overlay->anchorX = box->x;
                overlay->anchorY = box->y;
                overlay->anchorW = box->w;
                overlay->anchorH = box->h;
            }
        }
        return;
    }
    guiSetShown(target, target.has<UiHidden>());
}

} // namespace

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
    if ((in.pointer.released & kPointerRight) != 0 && state.capture == 0) {
        const flecs::entity target = guiAncestorWith<UiClickable>(guiEntityOf(world, state.under));
        if (target.id() != 0) {
            guiPushAction(state, target.id(), kGuiActionSecondary);
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
                if (const UiToggle* toggle = pressed.try_get<UiToggle>()) {
                    applyToggle(world, pressed, *toggle);
                }
            }
        }
    }
}

} // namespace burnhope::gui
