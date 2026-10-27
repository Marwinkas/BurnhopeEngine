#include "gui/interaction/InteractHover.hpp"

#include "gui/input/GuiHitTest.hpp"

namespace burnhope::gui {

namespace {

void writeTint(flecs::entity e, float level) {
    UiPaint* paint = e.try_get_mut<UiPaint>();
    if (paint != nullptr) {
        paint->tint[0] = level;
        paint->tint[1] = level;
        paint->tint[2] = level;
    }
}

} // namespace

void interactHover(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style) {
    state.pointerX = in.pointer.x;
    state.pointerY = in.pointer.y;

    flecs::entity under;
    flecs::entity target = guiHitTestEx(world, in.pointer.x, in.pointer.y, &under);
    state.under = under.id();
    const flecs::entity captured = guiEntityOf(world, state.capture);
    if (captured.id() != 0) {
        target = captured;
    }

    if (state.hover != target.id()) {
        const flecs::entity old = guiEntityOf(world, state.hover);
        if (old.id() != 0) {
            old.remove<UiHovered>();
        }
        if (target.id() != 0) {
            target.add<UiHovered>();
        }
        state.hover = target.id();
    }

    flecs::entity want;
    if (target.id() != 0 && target.has<UiHoverable>()) {
        want = target;
    }
    const uint8_t pressed = (want.id() != 0 && (state.press == want.id() || state.capture == want.id())) ? 1 : 0;
    if (state.tinted == want.id() && state.tintPressed == pressed) {
        return;
    }
    if (state.tinted != want.id()) {
        const flecs::entity prev = guiEntityOf(world, state.tinted);
        if (prev.id() != 0) {
            writeTint(prev, 1.0f);
        }
    }
    if (want.id() != 0) {
        const UiClass* cls = want.try_get<UiClass>();
        const float a = guiStyleHoverAlpha(style, cls != nullptr ? cls->id : kGuiNoClass);
        writeTint(want, pressed != 0 ? 1.0f - a : 1.0f + a);
    }
    state.tinted = want.id();
    state.tintPressed = pressed;
}

} // namespace burnhope::gui
