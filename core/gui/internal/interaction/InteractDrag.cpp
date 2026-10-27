#include "interaction/InteractDrag.hpp"

#include <algorithm>
#include <cmath>

#include "input/GuiHitTest.hpp"

namespace burnhope::gui {

namespace {

// `UiDraggable::value` is normalized (0..1); `minV`/`maxV` only map it to the real range.
float clampTo(float v, const UiDraggable&) {
    return std::clamp(v, 0.0f, 1.0f);
}

void release(flecs::world& world, GuiInteractState& state, bool push) {
    const flecs::entity e = guiEntityOf(world, state.capture);
    if (e.id() != 0) {
        UiDragState* ds = e.try_get_mut<UiDragState>();
        if (ds != nullptr) {
            ds->active = 0;
        }
        if (push) {
            guiPushAction(state, e.id(), kGuiActionDragEnd);
        }
    }
    state.capture = 0;
}

// Normalized outputs per axis: axis 0 x->value, axis 1 y->value, axis 2 x->value and y->valueY.
void apply(const InputFrame& in, UiDraggable& drag, UiDragState& ds, const UiBox& box) {
    const bool horizontal = drag.axis != 1;
    const bool vertical = drag.axis != 0;
    float* const outH = &drag.value;
    float* const outV = drag.axis == 1 ? &drag.value : &drag.valueY;
    const float startH = ds.startValue;
    const float startV = drag.axis == 1 ? ds.startValue : ds.startValueY;
    const float w = std::max(box.w, 1.0f);
    const float h = std::max(box.h, 1.0f);
    if (drag.mode == 0) {
        ds.dx = in.pointer.x - ds.originX;
        ds.dy = in.pointer.y - ds.originY;
        if (horizontal) {
            *outH = clampTo((in.pointer.x - box.x) / w, drag);
        }
        if (vertical) {
            *outV = clampTo((in.pointer.y - box.y) / h, drag);
        }
        return;
    }
    if (drag.mode == 1) {
        ds.dx = in.pointer.x - ds.originX;
        ds.dy = in.pointer.y - ds.originY;
        if (horizontal) {
            *outH = clampTo(startH + ds.dx / w, drag);
        }
        if (vertical) {
            *outV = clampTo(startV + ds.dy / h, drag);
        }
        return;
    }
    if (ds.latched == 0) {
        const float tx = horizontal ? std::fabs(in.pointer.x - ds.originX) : 0.0f;
        const float ty = vertical ? std::fabs(in.pointer.y - ds.originY) : 0.0f;
        if (tx >= kGuiScrubDeadzone || ty >= kGuiScrubDeadzone) {
            ds.latched = 1;
        }
        return;
    }
    const float k = (drag.maxV - drag.minV) * kGuiScrubPerPixel * (in.shift != 0 ? kGuiScrubFine : 1.0f);
    ds.dx += in.pointer.dx * k;
    ds.dy += in.pointer.dy * k;
    if (horizontal) {
        *outH = clampTo(startH + ds.dx, drag);
    }
    if (vertical) {
        *outV = clampTo(startV + ds.dy, drag);
    }
}

} // namespace

void interactDrag(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    if (state.capture != 0) {
        const flecs::entity cap = guiEntityOf(world, state.capture);
        if (cap.id() == 0 || !cap.has<UiDraggable>() || guiIsHidden(cap)) {
            release(world, state, false);
        }
    }

    if (state.capture == 0 && (in.pointer.pressed & kPointerLeft) != 0) {
        const flecs::entity target = guiAncestorWith<UiDraggable>(guiEntityOf(world, state.under));
        if (target.id() != 0 && !guiIsHidden(target)) {
            const UiDraggable* d = target.try_get<UiDraggable>();
            const UiBox* box = target.try_get<UiBox>();
            if (d != nullptr && box != nullptr) {
                UiDragState& ds = target.ensure<UiDragState>();
                ds = UiDragState{.originX = in.pointer.x,
                                 .originY = in.pointer.y,
                                 .startValue = d->value,
                                 .startValueY = d->valueY,
                                 .active = 1,
                                 .latched = static_cast<uint8_t>(d->mode == 2 ? 0 : 1)};
                state.capture = target.id();
            }
        }
    }

    if (state.capture == 0) {
        return;
    }
    const flecs::entity e = guiEntityOf(world, state.capture);
    UiDraggable* drag = e.try_get_mut<UiDraggable>();
    UiDragState* ds = e.try_get_mut<UiDragState>();
    const UiBox* box = e.try_get<UiBox>();
    if (drag == nullptr || ds == nullptr || box == nullptr) {
        release(world, state, false);
        return;
    }
    if (in.escape != 0) {
        drag->value = ds->startValue;
        drag->valueY = ds->startValueY;
        release(world, state, false);
        return;
    }
    const bool held = (in.pointer.down & kPointerLeft) != 0;
    apply(in, *drag, *ds, *box);
    if (!held) {
        release(world, state, true);
    }
}

} // namespace burnhope::gui
