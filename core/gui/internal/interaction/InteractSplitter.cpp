#include "interaction/InteractSplitter.hpp"

#include <algorithm>

#include "layout/GuiLayoutEngine.hpp"

namespace burnhope::gui {
namespace {

struct SplitterQuery {
    flecs::query<UiSplitter> splitters;
};

const SplitterQuery& splitterQuery(flecs::world& world) {
    if (const SplitterQuery* cached = world.try_get<SplitterQuery>()) {
        return *cached;
    }
    world.set<SplitterQuery>({world.query<UiSplitter>()});
    return *world.try_get<SplitterQuery>();
}

uint8_t cursorOf(const UiSplitter& s) {
    return s.horizontal != 0 ? kGuiCursorNorthSouth : kGuiCursorEastWest;
}

float axisOf(const UiSplitter& s, const InputFrame& in) {
    return s.horizontal != 0 ? in.pointer.y : in.pointer.x;
}

void resize(flecs::entity target, const UiSplitter& s, float size) {
    const UiFlex* flex = target.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    if (s.horizontal != 0) {
        YGNodeStyleSetHeight(flex->node, size);
    } else {
        YGNodeStyleSetWidth(flex->node, size);
    }
}

} // namespace

flecs::entity guiSplitterTarget(flecs::world& world, flecs::entity splitter) {
    const UiFlex* flex = splitter.try_get<UiFlex>();
    const UiSplitter* s = splitter.try_get<UiSplitter>();
    if (flex == nullptr || flex->node == nullptr || s == nullptr) {
        return flecs::entity();
    }
    const YGNodeRef parent = YGNodeGetParent(flex->node);
    if (parent == nullptr) {
        return flecs::entity();
    }
    const size_t count = YGNodeGetChildCount(parent);
    for (size_t i = 0; i < count; ++i) {
        if (YGNodeGetChild(parent, i) != flex->node) {
            continue;
        }
        if (s->resizeNext != 0) {
            return i + 1 < count ? guiLayoutEntity(world, YGNodeGetChild(parent, i + 1)) : flecs::entity();
        }
        return i > 0 ? guiLayoutEntity(world, YGNodeGetChild(parent, i - 1)) : flecs::entity();
    }
    return flecs::entity();
}

void interactSplitter(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    flecs::entity held = guiEntityOf(world, state.split);
    if (held.id() != 0 && (!held.has<UiSplitter>() || held.has<UiHidden>())) {
        state.split = 0;
        held = flecs::entity();
    }
    if (held.id() == 0 && (in.pointer.pressed & kPointerLeft) != 0 && state.capture == 0) {
        const flecs::entity under = guiEntityOf(world, state.under);
        const flecs::entity target = under.id() != 0 && under.has<UiSplitter>() ? guiSplitterTarget(world, under) : flecs::entity();
        const UiBox* box = target.id() != 0 ? target.try_get<UiBox>() : nullptr;
        if (box != nullptr) {
            const UiSplitter& s = *under.try_get<UiSplitter>();
            state.split = under.id();
            state.splitOrigin = axisOf(s, in);
            state.splitStart = s.horizontal != 0 ? box->h : box->w;
            held = under;
        }
    }
    if (held.id() == 0) {
        const flecs::entity under = guiEntityOf(world, state.under);
        if (under.id() != 0 && under.has<UiSplitter>()) {
            state.cursor = cursorOf(*under.try_get<UiSplitter>());
        }
        return;
    }
    const UiSplitter s = *held.try_get<UiSplitter>();
    state.cursor = cursorOf(s);
    if ((in.pointer.down & kPointerLeft) == 0) {
        state.split = 0;
        return;
    }
    const flecs::entity target = guiSplitterTarget(world, held);
    if (target.id() == 0) {
        state.split = 0;
        return;
    }
    const float travel = axisOf(s, in) - state.splitOrigin;
    float size = state.splitStart + (s.resizeNext != 0 ? -travel : travel);
    size = std::max(size, s.minSize);
    if (s.maxSize > 0.0f) {
        size = std::min(size, s.maxSize);
    }
    resize(target, s, size);
}

void guiSplitterRefresh(flecs::world& world) {
    flecs::entity grips[kGuiSplitterCap];
    uint32_t n = 0;
    splitterQuery(world).splitters.each([&](flecs::entity e, UiSplitter&) {
        if (n < kGuiSplitterCap) {
            grips[n++] = e;
        }
    });
    for (uint32_t i = 0; i < n; ++i) {
        const flecs::entity target = guiSplitterTarget(world, grips[i]);
        const bool want = target.id() != 0 && !target.has<UiHidden>();
        if (grips[i].has<UiHidden>() == want) {
            guiSetShown(grips[i], want);
        }
    }
}

} // namespace burnhope::gui
