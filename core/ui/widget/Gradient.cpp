#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Gradient.hpp"

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
    uiNotifyVisualDirty(*s);
}

} // namespace

uint16_t uiGradient(UiState& s, uint16_t parent) {
    UiFlex flex{};
    flex.heightMode = UiSize::Px;
    flex.height = 36.0f;
    flex.shrink = 0.0f;
    const uint16_t id = uiPanel(s, parent, flex, paintToken(s.look, UiToken::Content, 6.0f));
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].set<UiGradient>({});
    uiDrag(s, id, onGradient, &s);
    return id;
}

bool uiGradientDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id >= s.count) {
        return false;
    }
    const UiGradient* grad = s.ent[id].try_get<UiGradient>();
    if (grad == nullptr) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    const int stops = grad->stops < 2 ? 2 : (grad->stops > 4 ? 4 : grad->stops);
    for (int band = 0; band < 24; ++band) {
        const float t0 = static_cast<float>(band) / 24.0f;
        int seg = 0;
        for (int sidx = 0; sidx < stops - 1; ++sidx) {
            if (t0 >= grad->t[sidx] && t0 <= grad->t[sidx + 1]) {
                seg = sidx;
            }
        }
        const float span = grad->t[seg + 1] - grad->t[seg];
        const float u = span > 0.0001f ? (t0 - grad->t[seg]) / span : 0.0f;
        const float cr = grad->r[seg] + (grad->r[seg + 1] - grad->r[seg]) * u;
        const float cg = grad->g[seg] + (grad->g[seg + 1] - grad->g[seg]) * u;
        const float cb = grad->b[seg] + (grad->b[seg + 1] - grad->b[seg]) * u;
        n = emitRect(dst, n, cap, box.x + t0 * box.w, box.y + box.h * 0.35f, box.w / 24.0f + 1.0f, box.h * 0.3f, cr, cg, cb, 1.0f, 0.0f, clip);
    }
    const float tr = s.look.text[0];
    const float tg = s.look.text[1];
    const float tb = s.look.text[2];
    for (int p = 0; p < stops; ++p) {
        n = emitRect(
            dst, n, cap, box.x + grad->t[p] * box.w - 5.0f, box.y + box.h * 0.35f - 4.0f, 10.0f, box.h * 0.3f + 8.0f, tr, tg, tb, 0.9f, 2.0f, clip);
    }
    return false;
}

} // namespace burnhope
