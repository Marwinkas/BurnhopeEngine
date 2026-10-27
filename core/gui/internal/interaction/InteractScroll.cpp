#include "interaction/InteractScroll.hpp"

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

// Sets the scroll offset so the thumb's top sits at `thumbTop`.
void dragThumb(UiScroll& scroll, const UiBox& box, float thumbTop) {
    const GuiBarGeometry g = guiScrollBar(box, scroll);
    if (g.range <= 0.0f) {
        return;
    }
    const float travel = std::max(1.0f, g.trackH - g.thumbH);
    scroll.offY = std::clamp((thumbTop - g.trackY) / travel, 0.0f, 1.0f) * g.range;
}

} // namespace

bool interactScrollBar(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    const bool held = (in.pointer.down & kPointerLeft) != 0;
    if (state.barDrag != 0) {
        const flecs::entity e = guiEntityOf(world, state.barDrag);
        UiScroll* scroll = e.id() != 0 ? e.try_get_mut<UiScroll>() : nullptr;
        const UiBox* box = e.id() != 0 ? e.try_get<UiBox>() : nullptr;
        if (scroll == nullptr || box == nullptr || (!held && (in.pointer.released & kPointerLeft) == 0)) {
            state.barDrag = 0;
            return false;
        }
        dragThumb(*scroll, *box, in.pointer.y - state.barGrab);
        if (!held) {
            state.barDrag = 0;
        }
        return true;
    }
    if ((in.pointer.pressed & kPointerLeft) == 0 || state.capture != 0) {
        return false;
    }
    for (flecs::entity e = guiEntityOf(world, state.under); e.id() != 0 && e.is_alive(); e = e.parent()) {
        UiScroll* scroll = e.has<UiScrollable>() ? e.try_get_mut<UiScroll>() : nullptr;
        const UiBox* box = scroll != nullptr ? e.try_get<UiBox>() : nullptr;
        if (box == nullptr) {
            continue;
        }
        const GuiBarGeometry g = guiScrollBar(*box, *scroll);
        const bool inBand = in.pointer.x >= box->x + box->w - kGuiBarHitW && in.pointer.x < box->x + box->w && in.pointer.y >= box->y &&
                            in.pointer.y < box->y + box->h;
        if (g.range <= 0.0f || !inBand) {
            continue;
        }
        const bool onThumb = in.pointer.y >= g.thumbY && in.pointer.y < g.thumbY + g.thumbH;
        state.barDrag = e.id();
        state.barGrab = onThumb ? in.pointer.y - g.thumbY : g.thumbH * 0.5f;
        dragThumb(*scroll, *box, in.pointer.y - state.barGrab);
        return true;
    }
    return false;
}

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
