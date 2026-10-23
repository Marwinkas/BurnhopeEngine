#include "ui/Hover.hpp"
#include "ui/Interaction.hpp"
#include "ui/State.hpp"

namespace burnhope {

void uiHover(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    s.ent[id].add<UiHoverable>();
}

bool kidHot(const UiState& s, uint16_t id) {
    const UiPaint* paint = id < s.count ? s.ent[id].try_get<UiPaint>() : nullptr;
    if (paint == nullptr || paint->hoverKids == 0 || s.hovered == kUiNone || s.hovered >= s.count) {
        return false;
    }
    for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == id) {
            return true;
        }
    }
    return false;
}

void uiHoverTint(const UiState& s, uint16_t id, float& r, float& g, float& b, float& a) {
    if (id >= s.count) {
        return;
    }
    const bool row = kidHot(s, id);
    if (!row && (!s.ent[id].has<UiHoverable>() || id != s.hovered)) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (row && paint != nullptr && paint->hasHoverColor()) {
        r = paint->hr;
        g = paint->hg;
        b = paint->hb;
        if (paint->ha >= 0.0f) {
            a = paint->ha;
        }
        return;
    }
    if (paint == nullptr) {
        return;
    }
    if (paint != nullptr && paint->disabled) {
        a *= 0.45f;
        return;
    }
    if (paint->hasHoverColor()) {
        r = paint->hr;
        g = paint->hg;
        b = paint->hb;
        if (paint->ha >= 0.0f) {
            a = paint->ha;
        }
        return;
    }
    const float lift = s.look.hover > 0.0f ? s.look.hover : 0.08f;
    r = r + lift > 1.0f ? 1.0f : r + lift;
    g = g + lift > 1.0f ? 1.0f : g + lift;
    b = b + lift > 1.0f ? 1.0f : b + lift;
    if (a < 0.12f) {
        a = 0.28f;
    }
}

} // namespace burnhope
