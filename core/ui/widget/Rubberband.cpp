#include "ui/widget/Detail.hpp"

namespace burnhope {
namespace {

bool overlaps(const UiBox& a, float x0, float y0, float x1, float y1) {
    const float left = x0 < x1 ? x0 : x1;
    const float right = x0 < x1 ? x1 : x0;
    const float top = y0 < y1 ? y0 : y1;
    const float bottom = y0 < y1 ? y1 : y0;
    return a.w > 1.0f && a.h > 1.0f && a.x < right && a.x + a.w > left && a.y < bottom && a.y + a.h > top;
}

} // namespace

uint16_t uiRubberband(UiState& s, uint16_t host, void (*pick)(void* user, uint16_t child), void* user) {
    if (host >= s.count) {
        return kUiNone;
    }
    UiFlex flex = px(1.0f, 1.0f);
    flex.position = 1;
    UiPaint ink = paintToken(s.look, UiRole::Panel, UiToken::Accent, 2.0f);
    ink.a = 0.25f;
    ink.borderW = 2.0f;
    ink.br = 0.45f;
    ink.bg = 0.62f;
    ink.bb = 0.95f;
    const uint16_t overlay = spawn(s, host, flex, ink);
    uiShow(s, overlay, false);
    s.ent[host].set<UiRubber>({overlay, 0, 0, 0, 0, 0, pick, user});
    return overlay;
}

bool uiRubberbandTrack(UiState& s, uint16_t host, const InputFrame& in) {
    auto* band = host < s.count ? s.ent[host].try_get_mut<UiRubber>() : nullptr;
    if (band == nullptr) {
        return false;
    }
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    bool empty = s.hovered == host;
    if (!empty && s.hovered != kUiNone) {
        if (uiWithin(s, s.hovered, host)) {
            const UiPaint* node = s.ent[s.hovered].try_get<UiPaint>();
            empty = node == nullptr || node->role != static_cast<uint8_t>(UiRole::Button);
        } else if (s.parentOf[host] != kUiNone && uiWithin(s, s.hovered, s.parentOf[host])) {
            empty = false;
        } else {
            empty = true;
        }
    }
    if (press && empty) {
        band->active = 1;
        band->x0 = band->x1 = in.pointer.x;
        band->y0 = band->y1 = in.pointer.y;
        uiShow(s, band->overlay, true);
        return true;
    }
    if (band->active == 0) {
        return false;
    }
    if (down) {
        band->x1 = in.pointer.x;
        band->y1 = in.pointer.y;
        const UiBox& home = s.box[host];
        const float x0 = band->x0 < band->x1 ? band->x0 : band->x1;
        const float y0 = band->y0 < band->y1 ? band->y0 : band->y1;
        const float x1 = band->x0 < band->x1 ? band->x1 : band->x0;
        const float y1 = band->y0 < band->y1 ? band->y1 : band->y0;
        uiPlace(s, band->overlay, true, x0 - home.x, y0 - home.y, x1 - x0, y1 - y0);
        return true;
    }
    if (release) {
        if (band->pick != nullptr) {
            for (uint16_t id = 0; id < s.count; ++id) {
                if (id == band->overlay) {
                    continue;
                }
                const bool under = uiWithin(s, s.parentOf[id], host);
                const UiPaint* paint = s.ent[id].try_get<UiPaint>();
                if (!under || paint == nullptr || paint->role != static_cast<uint8_t>(UiRole::Button)) {
                    continue;
                }
                if (overlaps(s.box[id], band->x0, band->y0, band->x1, band->y1)) {
                    band->pick(band->user, id);
                }
            }
        }
        band->active = 0;
        uiShow(s, band->overlay, false);
        return true;
    }
    return false;
}

} // namespace burnhope
