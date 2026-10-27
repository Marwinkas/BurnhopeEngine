#include "gui/interaction/InteractFocus.hpp"

#include "gui/input/GuiHitTest.hpp"

namespace burnhope::gui {

namespace {

bool focusable(flecs::entity e) {
    return e.has<UiFocusable>() || e.has<UiEditable>();
}

struct Ring {
    uint64_t current;
    uint64_t first;
    uint64_t last;
    uint64_t before; // last focusable seen before `current`
    uint64_t after;  // first focusable seen after `current`
    bool seen;
};

GuiWalk ringVisit(flecs::entity e, void* user) {
    auto& r = *static_cast<Ring*>(user);
    if (!focusable(e)) {
        return GuiWalk::Continue;
    }
    const uint64_t id = e.id();
    if (r.first == 0) {
        r.first = id;
    }
    r.last = id;
    if (id == r.current) {
        r.seen = true;
    } else if (!r.seen) {
        r.before = id;
    } else if (r.after == 0) {
        r.after = id;
    }
    return GuiWalk::Continue;
}

} // namespace

void guiSetFocus(flecs::world& world, GuiInteractState& state, uint64_t id) {
    if (id == state.focus) {
        return;
    }
    const flecs::entity old = guiEntityOf(world, state.focus);
    if (old.id() != 0) {
        old.remove<UiFocused>();
    }
    const flecs::entity next = guiEntityOf(world, id);
    if (next.id() != 0) {
        next.add<UiFocused>();
    }
    state.focus = next.id();
    state.caretTick = 0;
}

void interactFocus(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    if (state.focus != 0) {
        const flecs::entity cur = guiEntityOf(world, state.focus);
        if (cur.id() == 0 || guiIsHidden(cur)) {
            guiSetFocus(world, state, 0);
        }
    }
    if ((in.pointer.pressed & kPointerLeft) != 0 && state.capture == 0) {
        flecs::entity pick = guiEntityOf(world, state.under);
        while (pick.id() != 0 && pick.is_alive() && !focusable(pick)) {
            pick = pick.parent();
        }
        guiSetFocus(world, state, pick.id());
    }
    if (in.escape != 0) {
        guiSetFocus(world, state, 0);
    }
    if (in.tab != 0) {
        Ring ring{.current = state.focus, .first = 0, .last = 0, .before = 0, .after = 0, .seen = false};
        guiWalkTree(world, ringVisit, &ring);
        uint64_t next = 0;
        if (in.shift != 0) {
            next = ring.before != 0 ? ring.before : ring.last;
        } else {
            next = ring.after != 0 ? ring.after : ring.first;
        }
        if (next != 0) {
            guiSetFocus(world, state, next);
        }
    }
    ++state.caretTick;
}

} // namespace burnhope::gui
