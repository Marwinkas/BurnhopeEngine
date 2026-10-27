#include "gui/interaction/InteractScroll.hpp"

#include <algorithm>

namespace burnhope::gui {

namespace {

float maxOffset(float content, float view) {
    return std::max(0.0f, content - view);
}

void clampScroll(UiScroll& s) {
    s.offX = std::clamp(s.offX, 0.0f, maxOffset(s.contentW, s.viewW));
    s.offY = std::clamp(s.offY, 0.0f, maxOffset(s.contentH, s.viewH));
}

} // namespace

void interactScroll(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    world.each([](UiScroll& s) { clampScroll(s); });
    if (state.capture != 0) {
        return;
    }
    float wheelY = in.wheel;
    float wheelX = in.wheelX;
    if (in.shift != 0 && wheelX == 0.0f) {
        wheelX = wheelY;
        wheelY = 0.0f;
    }
    if (wheelX == 0.0f && wheelY == 0.0f) {
        return;
    }
    flecs::entity e = guiEntityOf(world, state.under);
    while (e.id() != 0 && e.is_alive()) {
        UiScroll* s = e.has<UiScrollable>() ? e.try_get_mut<UiScroll>() : nullptr;
        if (s != nullptr) {
            const float before = s->offY;
            const float beforeX = s->offX;
            if (wheelY != 0.0f && maxOffset(s->contentH, s->viewH) > 0.0f) {
                s->offY -= wheelY * kGuiWheelStep;
            }
            if (wheelX != 0.0f && maxOffset(s->contentW, s->viewW) > 0.0f) {
                s->offX -= wheelX * kGuiWheelStep;
            }
            clampScroll(*s);
            if (s->offY != before || s->offX != beforeX) {
                return;
            }
            // Pinned at an edge or not scrollable on this axis: let an outer scroller take the wheel.
        }
        e = e.parent();
    }
}

} // namespace burnhope::gui
