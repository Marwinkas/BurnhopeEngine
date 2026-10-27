#pragma once

#include <algorithm>

#include "gui/data/GuiComponents.hpp"

namespace burnhope::gui {

// Clip semantics
// --------------
// Every entity E that has a UiBox receives a UiClip, the region of the screen where E may be seen or hit:
//
//     UiClip(E)    = Inherited(parent(E)) ∩ UiBox(E)
//     Inherited(P) = UiClipChildren on P ? UiClip(P) : Inherited(parent(P))
//     Inherited(none) = the screen rect {0, 0, screenW, screenH}
//
// So a node is always cut by the nearest clipping ancestors (scroll views, popups) and the screen, while a
// non-clipping ancestor never cuts its descendants (their overflow stays visible). Entities without a UiBox pass the
// inherited clip through unchanged. A UiClip with w <= 0 or h <= 0 is fully clipped (guiClipEmpty).
//
// The walk starts at every UiRoot entity and descends the flecs ChildOf hierarchy, parents before children.
// UiBox must be current (run guiLayoutRun first). Entities not reachable from a UiRoot keep their previous UiClip.
// Missing UiClip components are added through a deferred batch, so the pass is safe to call between frames only.
void guiScissorRun(flecs::world& w, float screenW, float screenH);

inline bool guiClipEmpty(const UiClip& c) {
    return c.w <= 0.0f || c.h <= 0.0f;
}

// Half-open test: x in [c.x, c.x + c.w), y in [c.y, c.y + c.h).
inline bool guiClipContains(const UiClip& c, float x, float y) {
    return x >= c.x && y >= c.y && x < c.x + c.w && y < c.y + c.h;
}

inline UiClip guiClipIntersect(const UiClip& a, const UiClip& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w);
    const float y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) {
        return UiClip{}; // fully clipped is always the canonical zero rect
    }
    return UiClip{.x = x0, .y = y0, .w = x1 - x0, .h = y1 - y0};
}

} // namespace burnhope::gui
