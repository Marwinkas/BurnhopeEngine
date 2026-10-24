#include "ui/Interaction.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "platform/Trace.hpp"

namespace burnhope {

bool insideParents(const UiState& s, uint16_t id, float x, float y) {
    UiBox clip{};
    if (!uiClipBox(s, id, clip)) {
        return true;
    }
    return x >= clip.x && y >= clip.y && x < clip.x + clip.w && y < clip.y + clip.h;
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
            if (p->a <= 0.02f && !s.ent[id].has<UiFocusable>() && p->hoverKids == 0) {
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
