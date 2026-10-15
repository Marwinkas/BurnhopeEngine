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

void uiHoverTint(const UiState& s, uint16_t id, float& r, float& g, float& b, float& a) {
    if (id >= s.count || !s.ent[id].has<UiHoverable>() || id != s.hovered) {
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
