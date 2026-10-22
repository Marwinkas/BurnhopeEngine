#include "ui/State.hpp"

#include <yoga/Yoga.h>

namespace burnhope {
namespace {

constexpr float kRevealSec = 0.18f;

float easeOutCubic(float u) {
    const float t = 1.0f - u;
    return 1.0f - t * t * t;
}

float revealEase(uint8_t phase, float u) {
    if (u < 0.0f) {
        u = 0.0f;
    }
    if (u > 1.0f) {
        u = 1.0f;
    }
    return phase == 2 ? u * u : easeOutCubic(u);
}

float revealNow(const UiState& s, const UiReveal& rev) {
    const float u = (s.time - rev.t0) / kRevealSec;
    return rev.from + (rev.to - rev.from) * revealEase(rev.phase, u);
}

void revealBox(UiState& s, uint16_t id, float height, float full) {
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->heightMode = UiSize::Px;
        flex->height = height > 0.0f ? height : 0.0f;
        flex->minH = 0.0f;
        flex->overflow = UiOverflow::Hidden;
    }
    const float span = full > 1.0f ? full : 1.0f;
    s.ent[id].set<UiFade>({height > 0.0f ? height / span : 0.0f});
}

} // namespace

void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a) {
    if (id >= kUiCap) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    UiAnim& anim = s.anim[id];
    anim.fr = paint != nullptr ? paint->r : r;
    anim.fg = paint != nullptr ? paint->g : g;
    anim.fb = paint != nullptr ? paint->b : b;
    anim.fa = paint != nullptr ? paint->a : a;
    if (anim.on != 0 && anim.dur > 0.0f) {
        float u = (s.time - anim.t0) / anim.dur;
        if (u < 0.0f) {
            u = 0.0f;
        }
        if (u > 1.0f) {
            u = 1.0f;
        }
        anim.fr += (anim.tr - anim.fr) * u;
        anim.fg += (anim.tg - anim.fg) * u;
        anim.fb += (anim.tb - anim.fb) * u;
        anim.fa += (anim.ta - anim.fa) * u;
    }
    anim.tr = r;
    anim.tg = g;
    anim.tb = b;
    anim.ta = a;
    anim.t0 = s.time;
    anim.dur = s.look.motion > 0.0f ? s.look.motion : 0.01f;
    anim.on = 1;
}

void uiReveal(UiState& s, uint16_t id, float full, bool open) {
    if (id >= s.count || s.yoga[id] == nullptr) {
        return;
    }
    UiReveal* cur = s.ent[id].try_get_mut<UiReveal>();
    if (full < 1.0f && cur != nullptr && cur->full > 1.0f) {
        full = cur->full;
    }
    if (full < 1.0f) {
        if (const UiFlex* flex = s.ent[id].try_get<UiFlex>()) {
            full = flex->height;
        }
    }
    if (full < 1.0f) {
        full = 1.0f;
    }
    const bool shown = YGNodeStyleGetDisplay(s.yoga[id]) != YGDisplayNone;
    if (open && cur != nullptr && cur->phase == 0 && cur->t0 < 0.0f && shown) {
        return;
    }
    if (!open && !shown && (cur == nullptr || cur->t0 < 0.0f)) {
        return;
    }
    if (cur != nullptr && cur->t0 >= 0.0f && ((open && cur->phase == 1) || (!open && cur->phase == 2))) {
        return;
    }
    float now = 0.0f;
    if (cur != nullptr && cur->t0 >= 0.0f) {
        now = revealNow(s, *cur);
    } else if (shown) {
        if (const UiFlex* flex = s.ent[id].try_get<UiFlex>()) {
            now = flex->height;
        }
    }
    UiReveal next{};
    next.full = full;
    next.from = open ? now : now;
    next.to = open ? full : 0.0f;
    next.t0 = s.time;
    next.phase = open ? 1 : 2;
    s.ent[id].set<UiReveal>(next);
    if (open) {
        uiShow(s, id, true);
    }
    revealBox(s, id, next.from, full);
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiRevealTick(UiState& s) {
    bool live = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        UiReveal* rev = s.ent[id].try_get_mut<UiReveal>();
        if (rev == nullptr || rev->t0 < 0.0f) {
            continue;
        }
        live = true;
        float u = (s.time - rev->t0) / kRevealSec;
        if (u < 0.0f) {
            u = 0.0f;
        }
        if (u > 1.0f) {
            u = 1.0f;
        }
        const float h = rev->from + (rev->to - rev->from) * revealEase(rev->phase, u);
        revealBox(s, id, h, rev->full);
        if (u >= 1.0f) {
            const bool shut = rev->to <= 0.5f;
            rev->t0 = -1.0f;
            rev->phase = 0;
            if (shut) {
                revealBox(s, id, 0.0f, rev->full);
                uiShow(s, id, false);
                s.ent[id].set<UiFade>({1.0f});
            } else {
                revealBox(s, id, rev->full, rev->full);
            }
        }
    }
    if (live) {
        s.layoutDirty = true;
        s.visualDirty = true;
    }
}

bool uiTickMotion(UiState& s) {
    bool live = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.anim[id].on != 0) {
            live = true;
            if (s.time - s.anim[id].t0 >= s.anim[id].dur) {
                s.anim[id].on = 0;
            }
        }
    }
    return live;
}

} // namespace burnhope
