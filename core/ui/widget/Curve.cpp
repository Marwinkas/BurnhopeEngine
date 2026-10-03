#include "ui/widget/Detail.hpp"

namespace burnhope {
namespace {

float clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

void onCurve(void* user, uint16_t id, float x, float y) {
    auto* s = static_cast<UiState*>(user);
    auto* curve = s->ent[id].try_get_mut<UiCurve>();
    if (curve == nullptr) {
        return;
    }
    const UiBox& box = s->box[id];
    if (box.w < 1.0f || box.h < 1.0f) {
        return;
    }
    const float u = clamp01((x - box.x) / box.w);
    const float v = clamp01((y - box.y) / box.h);
    int hot = 0;
    float best = 1.0e9f;
    for (int i = 0; i < 4; ++i) {
        const float dx = curve->x[i] - u;
        const float dy = curve->y[i] - v;
        const float d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            hot = i;
        }
    }
    curve->x[hot] = u;
    curve->y[hot] = v;
    s->visualDirty = true;
}

void onGradient(void* user, uint16_t id, float x, float y) {
    (void)y;
    auto* s = static_cast<UiState*>(user);
    auto* grad = s->ent[id].try_get_mut<UiGradient>();
    if (grad == nullptr) {
        return;
    }
    const UiBox& box = s->box[id];
    if (box.w < 1.0f) {
        return;
    }
    const float u = clamp01((x - box.x) / box.w);
    const int stops = grad->stops < 2 ? 2 : (grad->stops > 4 ? 4 : grad->stops);
    int hot = 0;
    float best = 1.0e9f;
    for (int i = 0; i < stops; ++i) {
        const float d = grad->t[i] - u;
        const float dd = d * d;
        if (dd < best) {
            best = dd;
            hot = i;
        }
    }
    grad->t[hot] = u;
    if (hot > 0 && grad->t[hot] < grad->t[hot - 1]) {
        grad->t[hot] = grad->t[hot - 1];
    }
    if (hot + 1 < stops && grad->t[hot] > grad->t[hot + 1]) {
        grad->t[hot] = grad->t[hot + 1];
    }
    s->visualDirty = true;
}

uint16_t track(UiState& s, uint16_t parent, float h) {
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = h;
    flex.shrink = 0.0f;
    return spawn(s, parent, flex, paint(UiRole::Panel, 0.14f, 0.15f, 0.18f, 6.0f));
}

} // namespace

uint16_t uiCurve(UiState& s, uint16_t parent) {
    const uint16_t id = track(s, parent, 120.0f);
    s.ent[id].set<UiCurve>({});
    uiDrag(s, id, onCurve, &s);
    return id;
}

uint16_t uiGradient(UiState& s, uint16_t parent) {
    const uint16_t id = track(s, parent, 36.0f);
    s.ent[id].set<UiGradient>({});
    uiDrag(s, id, onGradient, &s);
    return id;
}

} // namespace burnhope
