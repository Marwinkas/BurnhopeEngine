#include "gui/input/GuiHitTest.hpp"

#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/layout/GuiScissor.hpp"

namespace burnhope::gui {

namespace {

constexpr uint32_t kRootCap = 32;

struct RootRef {
    uint64_t id;
    uint32_t layer;
};

struct WalkCtx {
    flecs::world* world;
    GuiWalkFn fn;
    void* user;
};

// Returns false once the callback asked to stop.
bool walkNode(const WalkCtx& ctx, flecs::entity e) {
    if (e.has<UiHidden>()) {
        return true;
    }
    const GuiWalk r = ctx.fn(e, ctx.user);
    if (r == GuiWalk::Stop) {
        return false;
    }
    if (r == GuiWalk::SkipChildren) {
        return true;
    }
    const UiFlex* flex = e.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return true;
    }
    const size_t count = YGNodeGetChildCount(flex->node);
    for (size_t i = 0; i < count; ++i) {
        const YGNodeRef child = YGNodeGetChild(flex->node, i);
        flecs::entity ce = guiLayoutEntity(*ctx.world, child);
        if (ce.id() == 0 || !ce.is_alive()) {
            continue;
        }
        if (!walkNode(ctx, ce)) {
            return false;
        }
    }
    return true;
}

struct HitCtx {
    float x;
    float y;
    uint64_t best;
};

GuiWalk hitVisit(flecs::entity e, void* user) {
    auto& hit = *static_cast<HitCtx*>(user);
    const UiClip* clip = e.try_get<UiClip>();
    if (clip == nullptr || !guiClipContains(*clip, hit.x, hit.y)) {
        return GuiWalk::Continue;
    }
    // A fully transparent panel (layout container, viewport window) does not occlude what is behind it.
    const UiPaint* paint = e.try_get<UiPaint>();
    if ((paint != nullptr && paint->tint[3] > 0.0f) || guiIsTarget(e)) {
        hit.best = e.id();
    }
    return GuiWalk::Continue;
}

} // namespace

void guiWalkTree(flecs::world& world, GuiWalkFn fn, void* user) {
    RootRef roots[kRootCap];
    uint32_t count = 0;
    world.each(world.id<UiRoot>(), [&](flecs::entity e) {
        if (count >= kRootCap || e.has<UiHidden>()) {
            return;
        }
        const UiFlex* flex = e.try_get<UiFlex>();
        if (flex == nullptr || flex->node == nullptr) {
            return;
        }
        uint32_t layer = 0;
        if (const UiOverlay* ov = e.try_get<UiOverlay>()) {
            if (ov->open == 0) {
                return;
            }
            layer = static_cast<uint32_t>(ov->layer) + 1u;
        }
        // Stable insertion by ascending layer.
        uint32_t at = count;
        while (at > 0 && roots[at - 1].layer > layer) {
            roots[at] = roots[at - 1];
            --at;
        }
        roots[at] = RootRef{.id = e.id(), .layer = layer};
        ++count;
    });
    const WalkCtx ctx{.world = &world, .fn = fn, .user = user};
    for (uint32_t i = 0; i < count; ++i) {
        if (!walkNode(ctx, world.entity(static_cast<flecs::entity_t>(roots[i].id)))) {
            return;
        }
    }
}

bool guiIsHidden(flecs::entity e) {
    while (e.id() != 0 && e.is_alive()) {
        if (e.has<UiHidden>()) {
            return true;
        }
        e = e.parent();
    }
    return false;
}

bool guiIsTarget(flecs::entity e) {
    return e.has<UiHoverable>() || e.has<UiClickable>() || e.has<UiDraggable>() || e.has<UiScrollable>() ||
           e.has<UiFocusable>() || e.has<UiEditable>();
}

flecs::entity guiHitTestEx(flecs::world& world, float x, float y, flecs::entity* under) {
    HitCtx hit{.x = x, .y = y, .best = 0};
    guiWalkTree(world, hitVisit, &hit);
    if (hit.best == 0) {
        if (under != nullptr) {
            *under = flecs::entity();
        }
        return flecs::entity();
    }
    flecs::entity occluder = world.entity(static_cast<flecs::entity_t>(hit.best));
    if (under != nullptr) {
        *under = occluder;
    }
    flecs::entity e = occluder;
    while (e.id() != 0 && e.is_alive()) {
        if (guiIsTarget(e)) {
            return e;
        }
        e = e.parent();
    }
    return flecs::entity();
}

flecs::entity guiHitTest(flecs::world& world, float x, float y) {
    return guiHitTestEx(world, x, y, nullptr);
}

} // namespace burnhope::gui
