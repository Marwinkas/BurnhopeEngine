#include "ui/widget/Detail.hpp"
#include "ui/widget/Rubberband.hpp"

namespace burnhope {
namespace {

bool overlaps(const UiBox& a, float x0, float y0, float x1, float y1);

void rubberPick(UiState& s, uint16_t host, UiRubber& band) {
    if (band.pick == nullptr) {
        return;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        if (id == band.overlay) {
            continue;
        }
        const bool under = uiWithin(s, s.parentOf[id], host);
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (!under || paint == nullptr || paint->role != static_cast<uint8_t>(UiRole::Button)) {
            continue;
        }
        if (overlaps(s.box[id], band.x0, band.y0, band.x1, band.y1)) {
            band.pick(band.user, id);
        }
    }
}

bool overlaps(const UiBox& a, float x0, float y0, float x1, float y1) {
    const float left = x0 < x1 ? x0 : x1;
    const float right = x0 < x1 ? x1 : x0;
    const float top = y0 < y1 ? y0 : y1;
    const float bottom = y0 < y1 ? y1 : y0;
    return a.w > 1.0f && a.h > 1.0f && a.x < right && a.x + a.w > left && a.y < bottom && a.y + a.h > top;
}

} // namespace

uint16_t uiRubberband(UiState& s, uint16_t host, void (*pick)(void* user, uint16_t child), void* user, void (*begin)(void* user)) {
    if (host >= s.count) {
        return kUiNone;
    }
    UiFlex flex = px(1.0f, 1.0f);
    flex.position = 1;
    UiPaint ink = paintToken(s.look, UiRole::Panel, UiToken::Accent, 2.0f);
    ink.a = 0.25f;
    ink.borderW = 2.0f;
    ink.br = s.look.accent[0][0];
    ink.bg = s.look.accent[0][1];
    ink.bb = s.look.accent[0][2];
    const uint16_t overlay = spawn(s, host, flex, ink);
    uiShow(s, overlay, false);
    s.ent[overlay].set<UiPopup>({});
    s.ent[host].set<UiRubber>(UiRubber{.overlay = overlay, .pick = pick, .user = user, .begin = begin});
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
    if (in.ctrl != 0 || in.shift != 0) {
        if (band->active != 0) {
            band->active = 0;
            uiShow(s, band->overlay, false);
        }
        return false;
    }
    bool empty = false;
    if (s.hovered == host || uiWithin(s, s.hovered, host)) {
        empty = true;
        for (uint16_t p = s.hovered; p != kUiNone && p != host && p < s.count; p = s.parentOf[p]) {
            const UiPaint* node = s.ent[p].try_get<UiPaint>();
            if (node != nullptr && node->role == static_cast<uint8_t>(UiRole::Button)) {
                empty = false;
                break;
            }
        }
    }
    if (press && empty && band->active == 0) {
        band->active = 1;
        band->x0 = band->x1 = in.pointer.x;
        band->y0 = band->y1 = in.pointer.y;
        return false;
    }
    if (band->active == 0) {
        return false;
    }
    if (down && band->active == 1) {
        const float dx = in.pointer.x - band->x0;
        const float dy = in.pointer.y - band->y0;
        if (dx * dx + dy * dy < 25.0f) {
            return false;
        }
        band->active = 2;
        if (band->begin != nullptr) {
            band->begin(band->user);
        }
        uiShow(s, band->overlay, true);
    }
    if (down && band->active == 2) {
        band->x1 = in.pointer.x;
        band->y1 = in.pointer.y;
        const UiBox& home = s.box[host];
        const float x0 = band->x0 < band->x1 ? band->x0 : band->x1;
        const float y0 = band->y0 < band->y1 ? band->y0 : band->y1;
        const float x1 = band->x0 < band->x1 ? band->x1 : band->x0;
        const float y1 = band->y0 < band->y1 ? band->y1 : band->y0;
        uiPlace(s, band->overlay, true, x0 - home.x, y0 - home.y, x1 - x0, y1 - y0);
        if (band->begin != nullptr) {
            band->begin(band->user);
        }
        rubberPick(s, host, *band);
        s.capture = host;
        return true;
    }
    if (release || !down) {
        const bool dragged = band->active == 2;
        if (!dragged && band->active == 1 && band->begin != nullptr) {
            band->begin(band->user);
        }
        if (dragged && band->pick != nullptr) {
            rubberPick(s, host, *band);
        }
        band->active = 0;
        uiShow(s, band->overlay, false);
        if (s.capture == host) {
            s.capture = kUiNone;
        }
        return dragged;
    }
    return false;
}

bool uiRubberInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    (void)cursor;
    if (phase != kUiInEarly) {
        return false;
    }
    uint16_t bandHost = id < s.count && s.ent[id].try_get<UiRubber>() != nullptr ? id : uiAncestorId<UiRubber>(s, id);
    if (bandHost == kUiNone && s.capture < s.count && s.ent[s.capture].is_alive() && s.ent[s.capture].try_get<UiRubber>() != nullptr) {
        bandHost = s.capture;
    }
    return bandHost != kUiNone && uiRubberbandTrack(s, bandHost, in);
}

} // namespace burnhope
