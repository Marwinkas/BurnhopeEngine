#include "ui/Bar.hpp"
#include "ui/Panel.hpp"
#include "ui/Interaction.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "ui/Template.hpp"

#include <bit>

namespace burnhope {

uint16_t uiPanel(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    return spawn(s, parent, flex, paint);
}

void uiSetToken(UiState& s, uint16_t id, UiToken token) {
    stamp(s, id, token);
}

namespace {

UiFlex* flexOf(UiState& s, uint16_t id) {
    return id < s.count ? s.ent[id].try_get_mut<UiFlex>() : nullptr;
}

UiPaint* paintOf(UiState& s, uint16_t id) {
    return id < s.count ? s.ent[id].try_get_mut<UiPaint>() : nullptr;
}

void sizeAxis(UiSize& mode, float& value, float v) {
    if (v < 0.0f) {
        return;
    }
    mode = v == 0.0f ? UiSize::Auto : UiSize::Px;
    value = v;
}

} // namespace

void uiSetSize(UiState& s, uint16_t id, float w, float h) {
    if (UiFlex* flex = flexOf(s, id)) {
        sizeAxis(flex->widthMode, flex->width, w);
        sizeAxis(flex->heightMode, flex->height, h);
        s.layoutDirty = true;
    }
}

void uiSetWidthPercent(UiState& s, uint16_t id, float percent) {
    if (UiFlex* flex = flexOf(s, id)) {
        flex->widthMode = UiSize::Percent;
        flex->width = percent;
        s.layoutDirty = true;
    }
}

void uiSetPad(UiState& s, uint16_t id, float pad) {
    if (UiFlex* flex = flexOf(s, id)) {
        flex->pad = pad;
        flex->padL = pad;
        flex->padR = pad;
        flex->padT = pad;
        flex->padB = pad;
        s.layoutDirty = true;
    }
}

void uiSetGrow(UiState& s, uint16_t id, float grow, float shrink, float minW) {
    if (UiFlex* flex = flexOf(s, id)) {
        flex->grow = grow < 0.0f ? flex->grow : grow;
        flex->shrink = shrink < 0.0f ? flex->shrink : shrink;
        flex->minW = minW < 0.0f ? flex->minW : minW;
        s.layoutDirty = true;
    }
}

void uiSetGap(UiState& s, uint16_t id, float gap) {
    if (UiFlex* flex = flexOf(s, id)) {
        flex->gap = gap;
        s.layoutDirty = true;
    }
}

void uiAddWidth(UiState& s, uint16_t id, float delta, float minW) {
    if (UiFlex* flex = flexOf(s, id)) {
        flex->widthMode = UiSize::Px;
        flex->width = flex->width + delta < minW ? minW : flex->width + delta;
        s.layoutDirty = true;
    }
}

float uiStyleWidth(const UiState& s, uint16_t id, float fallback) {
    const UiFlex* flex = id < s.count ? s.ent[id].try_get<UiFlex>() : nullptr;
    return flex != nullptr ? flex->width : fallback;
}

void uiSetIcon(UiState& s, uint16_t id, UiIcon icon) {
    if (UiPaint* paint = paintOf(s, id)) {
        paint->icon = static_cast<uint8_t>(icon);
        s.visualDirty = true;
    }
}

void uiSetActive(UiState& s, uint16_t id, bool on) {
    UiPaint* paint = paintOf(s, id);
    if (paint == nullptr) {
        return;
    }
    paint->picked = on ? 1 : 0;
    if (paint->activeToken != UiToken::Custom) {
        paint->token = on ? paint->activeToken : paint->idleToken;
        tokenRgb(s.look, paint->token, paint->r, paint->g, paint->b);
    }
    s.visualDirty = true;
}

uint16_t uiSpacer(UiState& s, uint16_t parent, const char* cls) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/spacer", cls);
    if (id != kUiNone) {
        uiSetGrow(s, id, 1.0f, 1.0f);
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
    float a,
    float glyphA) {
    if (id >= s.count) {
        return n;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr) {
        return n;
    }
    const UiBox& box = s.box[id];
    const UiPaintStyle& ps = s.paintStyle;
    if (paint->disabled) {
        a *= ps.disabledAlpha;
    }
    if (paint->shadow > 0.5f && a > kUiInvisibleAlpha) {
        const float spread = paint->shadow * ps.shadowGrow;
        n = emitMark(
            dst, n, cap, box.x - spread, box.y - paint->shadow * ps.shadowDy, box.w + spread * 2.0f, box.h + spread * 2.0f,
            0.0f, 0.0f, 0.0f, ps.shadowAlpha, paint->radius + spread, spread, kUiFlagShadow, clip);
    }
    const uint32_t before = n;
    if (paint->ramp != UiRamp::None) {
        uint32_t mode = static_cast<uint32_t>(paint->ramp);
        uint32_t kind = kUiFlagRamp;
        if (const UiRampDraw* drawn = s.ent[id].try_get<UiRampDraw>()) {
            mode = drawn->mode;
            kind = drawn->flags != 0 ? drawn->flags : kUiFlagRamp;
        }
        n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, r, g, b, mode, kind);
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
    const UiText* spinLabel = s.ent[id].try_get<UiText>();
    const bool labeledRow = spinLabel != nullptr && spinLabel->len > 0;
    const float fillAngle = labeledRow ? 0.0f : paint->angle;
    const bool transformed = fillAngle != 0.0f || paint->scaleX != 1.0f || paint->scaleY != 1.0f
        || paint->bright != 1.0f || paint->contrast != 1.0f;
    if (n > before && transformed && clip == nullptr) {
        UiPrimitive& filled = dst[n - 1];
        filled.flags |= kUiFlagTransform;
        uiPackTransform(filled.pad0, filled.pad1, fillAngle, paint->scaleX, paint->scaleY, paint->bright, paint->contrast);
    }
    if (paint->borderW > 0.4f) {
        n = emitMark(
            dst, n, cap, box.x, box.y, box.w, box.h,
            paint->br, paint->bg, paint->bb, a > 0.2f ? a : 1.0f,
            paint->radius, paint->borderW, kUiFlagStroke, clip);
    }
    if (paint->icon != 0 && n < cap) {
        const float fit = box.w < box.h ? box.w : box.h;
        const bool ghost = paint->a <= kUiInvisibleAlpha && paint->borderW < kUiBorderMinW;
        float side = paint->plate != 0 ? ps.toolIcon : (ghost ? fit : (fit > ps.iconFitMin ? fit - ps.iconFitPad : ps.iconSide));
        if (side > ps.iconMax) {
            side = ps.iconMax;
        }
        if (side < ps.iconMin) {
            side = ps.iconMin;
        }
        const UiText* label = s.ent[id].try_get<UiText>();
        const bool labeled = label != nullptr && label->len > 0;
        if (labeled) {
            side = paint->iconLeft != 0 ? (paint->iconSide != 0 ? static_cast<float>(paint->iconSide) : ps.iconLeft) : ps.iconSide;
        }
        float iconX = box.x + (box.w - side) * 0.5f;
        if (labeled && paint->iconLeft != 0) {
            iconX = box.x + static_cast<float>(paint->iconLeft);
        } else if (labeled) {
            iconX = box.x + box.w - side - ps.iconInset;
            if (iconX < box.x) {
                iconX = box.x;
            }
        }
        UiPrimitive& icon = dst[n++];
        icon = {};
        icon.posX = iconX;
        icon.posY = box.y + (box.h - side) * 0.5f;
        icon.sizeX = side;
        icon.sizeY = side;
        const bool glyph = paint->a <= 0.02f;
        const bool tool = paint->plate != 0;
        icon.color[0] = glyph ? (tool ? r : paint->r) : s.look.text[0];
        icon.color[1] = glyph ? (tool ? g : paint->g) : s.look.text[1];
        icon.color[2] = glyph ? (tool ? b : paint->b) : s.look.text[2];
        icon.color[3] = glyphA > 0.0f ? glyphA : 1.0f;
        icon.texId = paint->icon;
        icon.flags = kUiFlagIcon;
        if (paint->angle != 0.0f) {
            icon.pad1 = std::bit_cast<uint32_t>(paint->angle);
        }
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

uint16_t uiCaptured(const UiState& s) {
    return s.capture;
}

uint16_t uiHovered(const UiState& s) {
    return s.hovered;
}

const UiLook& uiLook(const UiState& s) {
    return s.look;
}

} // namespace burnhope
