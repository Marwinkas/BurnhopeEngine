#include "ui/Interaction.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "platform/Trace.hpp"

#include <yoga/Yoga.h>

namespace burnhope {

namespace {

void intersectInto(UiBox& out, const UiBox& box) {
    const float x0 = out.x > box.x ? out.x : box.x;
    const float y0 = out.y > box.y ? out.y : box.y;
    const float x1 = out.x + out.w < box.x + box.w ? out.x + out.w : box.x + box.w;
    const float y1 = out.y + out.h < box.y + box.h ? out.y + out.h : box.y + box.h;
    out.x = x0;
    out.y = y0;
    out.w = x1 > x0 ? x1 - x0 : 0.0f;
    out.h = y1 > y0 ? y1 - y0 : 0.0f;
}

bool clipsChildren(const UiState& s, uint16_t id) {
    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
    const UiScissor* scissor = s.ent[id].try_get<UiScissor>();
    return (scissor != nullptr && scissor->enabled)
        || (flex != nullptr && (flex->overflow == UiOverflow::Hidden || flex->overflow == UiOverflow::Scroll));
}

// Derives one node's cache from its (already resolved) parent.
void resolveVis(UiState& s, uint16_t id) {
    UiNodeVis& v = s.vis[id];
    const uint16_t parent = s.parentOf[id];
    const bool hasParent = parent != kUiNone && parent < s.count;
    const bool ownHidden = s.tight[id] != 0 || (s.yoga[id] != nullptr && YGNodeStyleGetDisplay(s.yoga[id]) == YGDisplayNone);
    v.hidden = static_cast<uint8_t>((hasParent && s.vis[parent].hidden != 0) || ownHidden);
    v.overlay = static_cast<uint8_t>((hasParent && s.vis[parent].overlay != 0) || (s.ent[id].is_alive() && uiOverlayHere(s, id)));
    v.clipped = 0;
    v.clip = {};
    if (!hasParent) {
        return;
    }
    v.clipped = s.vis[parent].clipped;
    v.clip = s.vis[parent].clip;
    if (s.ent[parent].is_alive() && clipsChildren(s, parent)) {
        if (v.clipped == 0) {
            v.clip = s.box[parent];
            v.clipped = 1;
        } else {
            intersectInto(v.clip, s.box[parent]);
        }
    }
}

} // namespace

// One top-down pass: every node's hidden / overlay / clip result comes from its parent in O(1).
void uiCacheVisibility(UiState& s) {
    BH_ZONE;
    uint8_t done[kUiCap]{};
    uint16_t chain[kUiCap];
    for (uint16_t id = 0; id < s.count; ++id) {
        uint16_t depth = 0;
        for (uint16_t p = id; p != kUiNone && p < s.count && done[p] == 0 && depth < kUiCap; p = s.parentOf[p]) {
            chain[depth++] = p;
        }
        while (depth > 0) {
            const uint16_t p = chain[--depth];
            resolveVis(s, p);
            done[p] = 1;
        }
    }
}

uint16_t hitTest(const UiState& s, float x, float y) {
    BH_ZONE;
    auto scan = [&](bool overlay) -> uint16_t {
        for (uint16_t n = s.count; n > 0; --n) {
            const uint16_t id = static_cast<uint16_t>(n - 1);
            const UiNodeVis& v = s.vis[id];
            if (v.hidden != 0 || (v.overlay != 0) != overlay || !hitBox(s.box[id], x, y)) {
                continue;
            }
            if (v.clipped != 0 && !(x >= v.clip.x && y >= v.clip.y && x < v.clip.x + v.clip.w && y < v.clip.y + v.clip.h)) {
                continue;
            }
            if (!s.ent[id].is_alive()) {
                continue;
            }
            const UiPaint* p = s.ent[id].try_get<UiPaint>();
            if (p == nullptr) {
                continue;
            }
            if (s.ent[id].has<UiNoHit>() || s.ent[id].has<UiPass>()) {
                continue;
            }
            if (p->a <= 0.02f && !s.ent[id].has<UiFocusable>() && p->hoverKids == 0) {
                continue;
            }
            return id;
        }
        return kUiNone;
    };
    const uint16_t top = scan(true);
    return top != kUiNone ? top : scan(false);
}

} // namespace burnhope
