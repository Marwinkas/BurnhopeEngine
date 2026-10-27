#include "overlay/GuiOverlay.hpp"

#include <algorithm>

namespace burnhope::gui {
namespace {

struct OverlayQuery {
    flecs::query<UiOverlay> overlays;
};

const OverlayQuery& overlayQuery(flecs::world& world) {
    if (const OverlayQuery* cached = world.try_get<OverlayQuery>()) {
        return *cached;
    }
    world.set<OverlayQuery>({world.query<UiOverlay>()});
    return *world.try_get<OverlayQuery>();
}

bool contains(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

bool boxContains(flecs::entity e, float x, float y) {
    const UiBox* b = e.try_get<UiBox>();
    return b != nullptr && contains(x, y, b->x, b->y, b->w, b->h);
}

void overlaySize(flecs::entity e, float& w, float& h) {
    w = 0.0f;
    h = 0.0f;
    if (const UiBox* b = e.try_get<UiBox>()) {
        w = b->w;
        h = b->h;
    }
    const UiFlex* flex = e.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    if (w <= 0.0f) {
        const YGValue v = YGNodeStyleGetWidth(flex->node);
        w = v.unit == YGUnitPoint ? v.value : 0.0f;
    }
    if (h <= 0.0f) {
        const YGValue v = YGNodeStyleGetHeight(flex->node);
        h = v.unit == YGUnitPoint ? v.value : 0.0f;
    }
}

// Uses the preferred origin, switches to the opposite one when the preferred side is too small and the opposite side has
// more room, then clamps the box inside [0, limit].
float fit(float preferred, float opposite, float size, float limit, float roomPreferred, float roomOpposite) {
    const bool flip = roomPreferred < size && roomOpposite > roomPreferred;
    return std::clamp(flip ? opposite : preferred, 0.0f, std::max(0.0f, limit - size));
}

void place(flecs::entity e, const UiOverlay& o, const InputFrame& in, float screenW, float screenH) {
    const UiFlex* flex = e.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    float w = 0.0f;
    float h = 0.0f;
    overlaySize(e, w, h);
    float ax = o.anchorX;
    float ay = o.anchorY;
    float aw = o.anchorW;
    float ah = o.anchorH;
    if (o.placement == 4 && ax == 0.0f && ay == 0.0f && aw == 0.0f && ah == 0.0f) {
        ax = in.pointer.x;
        ay = in.pointer.y;
    }
    float x = 0.0f;
    float y = 0.0f;
    switch (o.placement) {
    case 1: // above
        y = fit(ay - h, ay + ah, h, screenH, ay, screenH - (ay + ah));
        x = std::clamp(ax, 0.0f, std::max(0.0f, screenW - w));
        break;
    case 2: // right
        x = fit(ax + aw, ax - w, w, screenW, screenW - (ax + aw), ax);
        y = std::clamp(ay, 0.0f, std::max(0.0f, screenH - h));
        break;
    case 3: // left
        x = fit(ax - w, ax + aw, w, screenW, ax, screenW - (ax + aw));
        y = std::clamp(ay, 0.0f, std::max(0.0f, screenH - h));
        break;
    case 5: // modal: centred on the screen whatever the anchor
        x = std::max(0.0f, (screenW - w) * 0.5f);
        y = std::max(0.0f, (screenH - h) * 0.5f);
        break;
    case 4: // at point: the box starts at the point and flips to its other side when it overflows
        x = fit(ax, ax - w, w, screenW, screenW - ax, ax);
        y = fit(ay, ay - h, h, screenH, screenH - ay, ay);
        break;
    default: // below
        y = fit(ay + ah, ay - h, h, screenH, screenH - (ay + ah), ay);
        x = std::clamp(ax, 0.0f, std::max(0.0f, screenW - w));
        break;
    }
    YGNodeRef node = flex->node;
    YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(node, YGEdgeLeft, x);
    YGNodeStyleSetPosition(node, YGEdgeTop, y);
}

} // namespace

void guiOverlayRun(flecs::world& world, const InputFrame& in, GuiInteractState& state, float screenW, float screenH) {
    flecs::entity items[kGuiOverlayCap];
    uint32_t n = 0;
    overlayQuery(world).overlays.each([&](flecs::entity e, UiOverlay&) {
        if (n < kGuiOverlayCap) {
            items[n++] = e;
        }
    });

    const float px = in.pointer.x;
    const float py = in.pointer.y;
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;

    if (press) {
        int hitLayer = -1;
        for (uint32_t i = 0; i < n; ++i) {
            const UiOverlay* o = items[i].try_get<UiOverlay>();
            if (o != nullptr && o->open != 0 && boxContains(items[i], px, py)) {
                hitLayer = std::max(hitLayer, static_cast<int>(o->layer));
            }
        }
        for (uint32_t i = 0; i < n; ++i) {
            UiOverlay* o = items[i].try_get_mut<UiOverlay>();
            if (o == nullptr || o->open == 0 || o->dismissOnOutside == 0) {
                continue;
            }
            const bool inside = boxContains(items[i], px, py) || contains(px, py, o->anchorX, o->anchorY, o->anchorW, o->anchorH);
            if (!inside && static_cast<int>(o->layer) >= hitLayer) {
                o->open = 0;
            }
        }
    }

    if (in.escape != 0) {
        int top = -1;
        uint32_t topIndex = n;
        for (uint32_t i = 0; i < n; ++i) {
            const UiOverlay* o = items[i].try_get<UiOverlay>();
            if (o != nullptr && o->open != 0 && o->dismissOnOutside != 0 && static_cast<int>(o->layer) >= top) {
                top = static_cast<int>(o->layer);
                topIndex = i;
            }
        }
        if (topIndex < n) {
            items[topIndex].try_get_mut<UiOverlay>()->open = 0;
        }
    }

    uint32_t open = 0;
    for (uint32_t i = 0; i < n; ++i) {
        flecs::entity e = items[i];
        const UiOverlay* o = e.try_get<UiOverlay>();
        if (o == nullptr) {
            continue;
        }
        const bool hidden = e.has<UiHidden>();
        if (o->open == 0) {
            if (!hidden) {
                e.add<UiHidden>();
            }
            continue;
        }
        ++open;
        if (hidden) {
            e.remove<UiHidden>();
        }
        o = e.try_get<UiOverlay>();
        place(e, *o, in, screenW, screenH);
    }
    state.overlayOpen = static_cast<uint8_t>(std::min<uint32_t>(open, 255u));
}

} // namespace burnhope::gui
