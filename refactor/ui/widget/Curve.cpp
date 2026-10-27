#include "ui/widget/Curve.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "rhi/UiPrimitive.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "ui/Template.hpp"
#include "ui/StyleSheet.hpp"

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
    uiNotifyVisualDirty(*s);
}

bool drawCurve(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    const UiCurve* curve = s.ent[id].try_get<UiCurve>();
    if (curve == nullptr) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    const UiRgb line = uiStyleTint(s, "curve", UiStyleTone::Line);
    const UiRgb grip = uiStyleTint(s, "curve", UiStyleTone::Handle);
    const int steps = static_cast<int>(uiStyle(s, "curve", UiStyleKey::Steps));
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
        prim.color[0] = line.r;
        prim.color[1] = line.g;
        prim.color[2] = line.b;
        prim.color[3] = 1.0f;
        prim.texId = static_cast<uint32_t>(step);
        prim.flags = kUiFlagRibbon;
    }
    const float handle = uiStyle(s, "curve", UiStyleKey::HandleSide);
    for (int p = 0; p < 4; ++p) {
        n = emitRect(dst, n, cap, box.x + curve->x[p] * box.w - handle * 0.5f, box.y + curve->y[p] * box.h - handle * 0.5f, handle, handle, grip.r, grip.g, grip.b, 1.0f, handle * 0.5f, clip);
    }
    return true;
}

} // namespace

uint16_t uiCurve(UiState& s, uint16_t parent) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/panel", "curve");
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].set<UiCurve>({});
    uiDrag(s, id, onCurve, &s);
    uiCurveBind(s);
    return id;
}

void uiCurveBind(UiState& s) {
    uiDrawDecorAdd(s, drawCurve);
}

} // namespace burnhope
