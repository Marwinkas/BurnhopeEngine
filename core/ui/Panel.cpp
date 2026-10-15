#include "ui/Panel.hpp"
#include "ui/Interaction.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"

#include <bit>

namespace burnhope {

uint16_t uiPanel(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    return spawn(s, parent, flex, paint);
}

uint16_t uiSpacer(UiState& s, uint16_t parent, const UiFlex& flex) {
    const uint16_t id = uiPanel(s, parent, flex, paintClear());
    if (id != kUiNone) {
        s.ent[id].add<UiPass>();
    }
    return id;
}

uint32_t uiEmitPanel(
    const UiState& s,
    uint16_t id,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox* clip,
    float r,
    float g,
    float b,
    float a) {
    if (id >= s.count) {
        return n;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr) {
        return n;
    }
    const UiBox& box = s.box[id];
    if (paint->disabled) {
        a *= 0.45f;
    }
    if (paint->shadow > 0.5f && a > 0.02f) {
        const float sh = paint->shadow;
        n = emitMark(
            dst, n, cap, box.x - sh, box.y - sh * 0.15f, box.w + sh * 2.0f, box.h + sh * 2.0f,
            0.0f, 0.0f, 0.0f, 0.42f, paint->radius + sh, sh, kUiFlagShadow, clip);
    }
    const uint32_t before = n;
    if (paint->ramp != UiRamp::None) {
        n = emitRamp(
            dst, n, cap, box.x, box.y, box.w, box.h, r, g, b,
            static_cast<uint32_t>(paint->ramp), kUiFlagRamp);
    } else if (a > 0.0f) {
        n = emitRect(dst, n, cap, box.x, box.y, box.w, box.h, r, g, b, a, paint->radius, clip);
    }
    if (n > before && (paint->radiusTr >= 0.0f || paint->radiusBr >= 0.0f || paint->radiusBl >= 0.0f)) {
        UiPrimitive& filled = dst[n - 1];
        const float tl = paint->radius;
        filled.corner[0] = tl;
        filled.corner[1] = paint->effectiveRadiusTr();
        filled.corner[2] = paint->effectiveRadiusBr();
        filled.corner[3] = paint->effectiveRadiusBl();
    }
    const bool transformed = paint->angle != 0.0f || paint->scaleX != 1.0f || paint->scaleY != 1.0f
        || paint->bright != 1.0f || paint->contrast != 1.0f;
    if (n > before && transformed && clip == nullptr) {
        UiPrimitive& filled = dst[n - 1];
        filled.flags |= kUiFlagTransform;
        uiPackTransform(filled.pad0, filled.pad1, paint->angle, paint->scaleX, paint->scaleY, paint->bright, paint->contrast);
    }
    if (paint->borderW > 0.4f) {
        n = emitMark(
            dst, n, cap, box.x, box.y, box.w, box.h,
            paint->br, paint->bg, paint->bb, a > 0.2f ? a : 1.0f,
            paint->radius, paint->borderW, kUiFlagStroke, clip);
    }
    if (paint->icon != 0 && n < cap) {
        const float fit = box.w < box.h ? box.w : box.h;
        float side = fit > 18.0f ? fit - 4.0f : 14.0f;
        if (side > 72.0f) {
            side = 72.0f;
        }
        if (side < 12.0f) {
            side = 12.0f;
        }
        UiPrimitive& icon = dst[n++];
        icon = {};
        icon.posX = box.x + (box.w - side) * 0.5f;
        icon.posY = box.y + (box.h - side) * 0.5f;
        icon.sizeX = side;
        icon.sizeY = side;
        const bool glyph = paint->a <= 0.02f;
        icon.color[0] = glyph ? paint->r : s.look.text[0];
        icon.color[1] = glyph ? paint->g : s.look.text[1];
        icon.color[2] = glyph ? paint->b : s.look.text[2];
        icon.color[3] = 1.0f;
        icon.texId = paint->icon;
        icon.flags = kUiFlagIcon;
        if (clip != nullptr) {
            icon.flags |= kUiFlagClip;
            icon.extra[1] = clip->x;
            icon.extra[2] = clip->y;
            icon.extra[3] = clip->w;
            icon.pad0 = std::bit_cast<uint32_t>(clip->h);
        }
    }
    return n;
}

} // namespace burnhope
