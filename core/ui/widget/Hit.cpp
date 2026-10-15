#include "ui/State.hpp"
#include "platform/Trace.hpp"

namespace burnhope {

bool insideParents(const UiState& s, uint16_t id, float x, float y) {
    bool any = false;
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        const UiScissor* scissor = s.ent[p].try_get<UiScissor>();
        const UiFlex* flex = s.ent[p].try_get<UiFlex>();
        const bool clip = (scissor != nullptr && scissor->enabled)
            || (flex != nullptr && (flex->overflow == UiOverflow::Hidden || flex->overflow == UiOverflow::Scroll));
        if (!clip) {
            continue;
        }
        const UiBox& box = s.box[p];
        if (!any) {
            x0 = box.x;
            y0 = box.y;
            x1 = box.x + box.w;
            y1 = box.y + box.h;
            any = true;
            continue;
        }
        if (box.x > x0) {
            x0 = box.x;
        }
        if (box.y > y0) {
            y0 = box.y;
        }
        if (box.x + box.w < x1) {
            x1 = box.x + box.w;
        }
        if (box.y + box.h < y1) {
            y1 = box.y + box.h;
        }
    }
    if (!any) {
        return true;
    }
    return x >= x0 && y >= y0 && x < x1 && y < y1;
}

uint16_t hitTest(const UiState& s, float x, float y) {
    BH_ZONE;
    auto scan = [&](bool overlay) -> uint16_t {
        for (uint16_t n = s.count; n > 0; --n) {
            const uint16_t id = static_cast<uint16_t>(n - 1);
            if (!s.ent[id].is_alive() || uiIsOverlay(s, id) != overlay) {
                continue;
            }
            const UiPaint* p = s.ent[id].try_get<UiPaint>();
            if (p == nullptr) {
                continue;
            }
            if (s.ent[id].has<UiNoHit>() || s.ent[id].has<UiPass>()) {
                continue;
            }
            if (!insideParents(s, id, x, y)) {
                continue;
            }
            bool hidden = false;
            for (uint16_t up = id; up != kUiNone && up < s.count; up = s.parentOf[up]) {
                if (s.tight[up] != 0 || !nodeShown(s, up)) {
                    hidden = true;
                    break;
                }
            }
            if (hidden) {
                continue;
            }
            if (hitBox(s.box[id], x, y)) {
                return id;
            }
        }
        return kUiNone;
    };
    const uint16_t top = scan(true);
    return top != kUiNone ? top : scan(false);
}

} // namespace burnhope
