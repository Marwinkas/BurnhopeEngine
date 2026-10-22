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

bool rowHot(const UiState& s, uint16_t id) {
    if (id >= s.count || s.hovered == kUiNone || s.hovered >= s.count || !s.ent[id].has<UiRowHover>()) {
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
    const bool row = rowHot(s, id);
    if (!row && (!s.ent[id].has<UiHoverable>() || id != s.hovered)) {
        return;
    }
    if (row) {
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
        a = 0.14f;
        return;
    }
    if (s.ent[id].has<UiIconChrome>()) {
        if (a < 0.12f) {
            a = 0.28f;
        }
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint != nullptr && paint->disabled) {
        a *= 0.45f;
        return;
    }
    if (paint != nullptr && paint->hasHoverColor()) {
        r = paint->hr;
        g = paint->hg;
        b = paint->hb;
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
