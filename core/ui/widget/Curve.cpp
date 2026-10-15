#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Curve.hpp"
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
    flex.heightMode = UiSize::Px;
    flex.height = h;
    flex.shrink = 0.0f;
    return spawn(s, parent, flex, paintToken(s.look, UiToken::Content, 6.0f));
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

bool uiCurveDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id >= s.count) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    if (const UiCurve* curve = s.ent[id].try_get<UiCurve>()) {
        const int steps = 16;
        for (int step = 0; step < steps && n < cap; ++step) {
            UiPrimitive& prim = dst[n++];
            prim = {};
            prim.posX = box.x + curve->x[0] * box.w;
            prim.posY = box.y + curve->y[0] * box.h;
            prim.sizeX = box.x + curve->x[1] * box.w;
            prim.sizeY = box.y + curve->y[1] * box.h;
            prim.extra[0] = box.x + curve->x[2] * box.w;
            prim.extra[1] = box.y + curve->y[2] * box.h;
            prim.extra[2] = box.x + curve->x[3] * box.w;
            prim.extra[3] = box.y + curve->y[3] * box.h;
            prim.color[0] = 0.95f;
            prim.color[1] = 0.82f;
            prim.color[2] = 0.35f;
            prim.color[3] = 1.0f;
            prim.texId = static_cast<uint32_t>(step);
            prim.flags = kUiFlagRibbon;
        }
        for (int p = 0; p < 4; ++p) {
            n = emitRect(dst, n, cap, box.x + curve->x[p] * box.w - 4.0f, box.y + curve->y[p] * box.h - 4.0f, 8.0f, 8.0f, 0.95f, 0.9f, 0.85f, 1.0f, 4.0f, clip);
        }
    }
    if (const UiGradient* grad = s.ent[id].try_get<UiGradient>()) {
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
        for (int p = 0; p < stops; ++p) {
            n = emitRect(dst, n, cap, box.x + grad->t[p] * box.w - 5.0f, box.y + box.h * 0.35f - 4.0f, 10.0f, box.h * 0.3f + 8.0f, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f, clip);
        }
    }
    return false;
}

} // namespace burnhope
