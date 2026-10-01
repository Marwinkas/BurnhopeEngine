#include "ui/Canvas.hpp"

#include <cmath>
#include "ui/Font.hpp"
#include "ui/State.hpp"
#include "rhi/Barrier.hpp"
#include "image/ImageFile.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

#include <yoga/YGValue.h>

namespace burnhope {
namespace {

YGFlexDirection flexDir(uint8_t direction) {
    if (direction == 1) {
        return YGFlexDirectionRow;
    }
    if (direction == 2) {
        return YGFlexDirectionColumnReverse;
    }
    if (direction == 3) {
        return YGFlexDirectionRowReverse;
    }
    return YGFlexDirectionColumn;
}

YGAlign flexAlign(uint8_t align) {
    if (align == 1) {
        return YGAlignCenter;
    }
    if (align == 2) {
        return YGAlignFlexStart;
    }
    if (align == 3) {
        return YGAlignFlexEnd;
    }
    if (align == 4) {
        return YGAlignBaseline;
    }
    return YGAlignStretch;
}

YGJustify flexJustify(uint8_t justify) {
    if (justify == 1) {
        return YGJustifyCenter;
    }
    if (justify == 2) {
        return YGJustifyFlexEnd;
    }
    if (justify == 3) {
        return YGJustifySpaceBetween;
    }
    if (justify == 4) {
        return YGJustifySpaceAround;
    }
    if (justify == 5) {
        return YGJustifySpaceEvenly;
    }
    return YGJustifyFlexStart;
}

YGAlign flexContent(uint8_t content) {
    if (content == 1) {
        return YGAlignCenter;
    }
    if (content == 2) {
        return YGAlignFlexStart;
    }
    if (content == 3) {
        return YGAlignFlexEnd;
    }
    if (content == 4) {
        return YGAlignSpaceBetween;
    }
    if (content == 5) {
        return YGAlignSpaceAround;
    }
    if (content == 6) {
        return YGAlignSpaceEvenly;
    }
    return YGAlignStretch;
}

YGAlign flexSelf(uint8_t self) {
    if (self == 1) {
        return YGAlignStretch;
    }
    if (self == 2) {
        return YGAlignCenter;
    }
    if (self == 3) {
        return YGAlignFlexStart;
    }
    if (self == 4) {
        return YGAlignFlexEnd;
    }
    if (self == 5) {
        return YGAlignBaseline;
    }
    return YGAlignAuto;
}

void applyFlex(YGNodeRef node, const UiFlex& f) {
    YGNodeStyleSetFlexDirection(node, flexDir(f.direction));
    YGNodeStyleSetFlexWrap(node, f.wrap == 1 ? YGWrapWrap : f.wrap == 2 ? YGWrapWrapReverse : YGWrapNoWrap);
    YGNodeStyleSetAlignItems(node, flexAlign(f.align));
    YGNodeStyleSetAlignContent(node, flexContent(f.content));
    YGNodeStyleSetAlignSelf(node, flexSelf(f.self));
    YGNodeStyleSetJustifyContent(node, flexJustify(f.justify));
    YGNodeStyleSetBoxSizing(node, f.box == 1 ? YGBoxSizingContentBox : YGBoxSizingBorderBox);
    if (f.widthMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetWidth(node, f.width);
    } else if (f.widthMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetWidthPercent(node, f.width);
    } else {
        YGNodeStyleSetWidthAuto(node);
    }
    if (f.heightMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetHeight(node, f.height);
    } else if (f.heightMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetHeightPercent(node, f.height);
    } else {
        YGNodeStyleSetHeightAuto(node);
    }
    YGNodeStyleSetFlexGrow(node, f.grow);
    YGNodeStyleSetFlexShrink(node, f.shrink);
    if (f.basisMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetFlexBasis(node, f.basis);
    } else if (f.basisMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetFlexBasisPercent(node, f.basis);
    } else {
        YGNodeStyleSetFlexBasisAuto(node);
    }
    if (f.maxWMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetMaxWidth(node, f.maxW);
    } else if (f.maxWMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetMaxWidthPercent(node, f.maxW);
    } else {
        YGNodeStyleSetMaxWidth(node, YGUndefined);
    }
    if (f.maxHMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetMaxHeight(node, f.maxH);
    } else if (f.maxHMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetMaxHeightPercent(node, f.maxH);
    } else {
        YGNodeStyleSetMaxHeight(node, YGUndefined);
    }
    YGNodeStyleSetAspectRatio(node, f.aspect > 0.0f ? f.aspect : YGUndefined);
    if (f.minW > 0.0f) {
        YGNodeStyleSetMinWidth(node, f.minW);
    } else if (f.grow > 0.0f) {
        YGNodeStyleSetMinWidth(node, 0.0f);
    }
    if (f.minH > 0.0f) {
        YGNodeStyleSetMinHeight(node, f.minH);
    } else if (f.grow > 0.0f) {
        YGNodeStyleSetMinHeight(node, 0.0f);
    }
    const bool sides = f.padL > 0.0f || f.padR > 0.0f || f.padT > 0.0f || f.padB > 0.0f;
    if (sides) {
        YGNodeStyleSetPadding(node, YGEdgeLeft, f.padL);
        YGNodeStyleSetPadding(node, YGEdgeRight, f.padR);
        YGNodeStyleSetPadding(node, YGEdgeTop, f.padT);
        YGNodeStyleSetPadding(node, YGEdgeBottom, f.padB);
    } else {
        YGNodeStyleSetPadding(node, YGEdgeAll, f.pad);
    }
    if (f.gapRow < 0.0f) {
        YGNodeStyleSetGap(node, YGGutterAll, f.gap);
    } else {
        YGNodeStyleSetGap(node, YGGutterColumn, f.gap);
        YGNodeStyleSetGap(node, YGGutterRow, f.gapRow);
    }
    YGNodeStyleSetMargin(node, YGEdgeLeft, f.marginL);
    YGNodeStyleSetMargin(node, YGEdgeRight, f.marginR);
    YGNodeStyleSetMargin(node, YGEdgeTop, f.marginT);
    YGNodeStyleSetMargin(node, YGEdgeBottom, f.marginB);
    if (f.position == 1) {
        YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
    } else if (f.position == 2) {
        YGNodeStyleSetPositionType(node, YGPositionTypeRelative);
    } else {
        YGNodeStyleSetPositionType(node, YGPositionTypeStatic);
    }
    if (f.position != 0) {
        YGNodeStyleSetPosition(node, YGEdgeLeft, f.posX);
        YGNodeStyleSetPosition(node, YGEdgeTop, f.posY);
        YGNodeStyleSetPosition(node, YGEdgeRight, (f.inset & 1) != 0 ? f.posR : YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeBottom, (f.inset & 2) != 0 ? f.posB : YGUndefined);
    } else {
        YGNodeStyleSetPosition(node, YGEdgeLeft, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeTop, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeRight, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeBottom, YGUndefined);
    }
    const YGOverflow over = f.overflow == 1 ? YGOverflowHidden : f.overflow == 2 ? YGOverflowScroll : YGOverflowVisible;
    YGNodeStyleSetOverflow(node, over);
}

void cacheBox(UiState& s, uint16_t id) {
    float x = 0.0f;
    float y = 0.0f;
    for (YGNodeRef node = s.yoga[id]; node != nullptr; node = YGNodeGetParent(node)) {
        x += YGNodeLayoutGetLeft(node);
        y += YGNodeLayoutGetTop(node);
    }
    s.box[id].x = x;
    s.box[id].y = y;
    s.box[id].w = YGNodeLayoutGetWidth(s.yoga[id]);
    s.box[id].h = YGNodeLayoutGetHeight(s.yoga[id]);
}

void putFloat(uint32_t& dst, float v) {
    std::memcpy(&dst, &v, sizeof(float));
}

uint32_t emitRect(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float w,
    float h,
    float r,
    float g,
    float b,
    float a,
    float radius,
    const UiBox* clip) {
    if (n >= cap || w < 0.5f || h < 0.5f || a <= 0.0f) {
        return n;
    }
    UiPrimitive& p = dst[n];
    p = {};
    p.posX = x;
    p.posY = y;
    p.sizeX = w;
    p.sizeY = h;
    p.color[0] = r;
    p.color[1] = g;
    p.color[2] = b;
    p.color[3] = a;
    p.extra[0] = radius;
    if (clip != nullptr) {
        p.flags |= kUiFlagClip;
        p.extra[1] = clip->x;
        p.extra[2] = clip->y;
        p.extra[3] = clip->w;
        putFloat(p.pad0, clip->h);
    }
    return n + 1;
}

uint32_t emitRamp(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float w,
    float h,
    float r,
    float g,
    float b,
    uint32_t mode,
    uint32_t kind) {
    if (n >= cap || w < 0.5f || h < 0.5f) {
        return n;
    }
    UiPrimitive& p = dst[n];
    p = {};
    p.posX = x;
    p.posY = y;
    p.sizeX = w;
    p.sizeY = h;
    p.color[0] = r;
    p.color[1] = g;
    p.color[2] = b;
    p.color[3] = 1.0f;
    p.texId = mode;
    p.flags = kind;
    p.extra[0] = 1.0f;
    return n + 1;
}

uint32_t emitGlyphs(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const char* text,
    uint8_t len,
    const UiState& ui,
    const UiBox& box,
    float r,
    float g,
    float b,
    const UiBox* clip,
    bool center,
    float alpha,
    float tracking,
    float spaceGap,
    float emScale) {
    const float scale = ui.fontReady && ui.fontPx > 1.0f ? ui.look.glyphH / ui.fontPx : 1.0f;
    const float em = ui.look.glyphH > 1.0f ? ui.look.glyphH : 18.0f;
    float fit = emScale > 0.05f ? emScale : 1.0f;
    if (emScale <= 0.05f && em > box.h && em > 0.5f) {
        fit = box.h / em;
    }
    const float used = scale * fit;
    const float baseline = box.y + (box.h - em * fit) * 0.5f + em * fit * 0.80f;
    float textW = uiMeasure(ui, text, len, len) * fit;
    float pen = box.x;
    if (center) {
        const float slack = box.w - textW;
        if (slack > 0.0f) {
            pen += slack * 0.5f;
        }
    }
    for (uint8_t i = 0; i < len && n < cap;) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(text, len, i, step);
        if (step == 0) {
            break;
        }
        int gi = uiGlyphIndex(cp);
        if (gi < 0 || gi >= static_cast<int>(kUiFontGlyphs)) {
            gi = static_cast<int>('?' - 32);
        }
        const float adv = ui.fontReady ? ui.glyphAdv[gi] * used : ui.look.advance;
        float gw = ui.fontReady ? ui.glyphW[gi] * used : ui.look.glyphW;
        float gh = ui.fontReady ? ui.glyphH[gi] * used : ui.look.glyphH;
        const float x = pen + (ui.fontReady ? ui.glyphBx[gi] * used : 0.0f);
        const float y = ui.fontReady ? baseline - ui.glyphBy[gi] * used : box.y + (box.h - gh) * 0.5f;
        pen += adv + tracking;
        if (cp == ' ') {
            pen += spaceGap;
        }
        i = static_cast<uint8_t>(i + step);
        if (gw < 0.5f || gh < 0.5f || x >= box.x + box.w || x + gw <= box.x) {
            continue;
        }
        UiPrimitive& p = dst[n++];
        p = {};
        p.posX = x;
        p.posY = y;
        p.sizeX = gw;
        p.sizeY = gh;
        p.color[0] = r;
        p.color[1] = g;
        p.color[2] = b;
        p.color[3] = alpha;
        p.texId = static_cast<uint32_t>(gi);
        p.flags = kUiFlagGlyph;
        if (clip != nullptr) {
            p.flags |= kUiFlagClip;
            p.extra[1] = clip->x;
            p.extra[2] = clip->y;
            p.extra[3] = clip->w;
            putFloat(p.pad0, clip->h);
        }
    }
    return n;
}

bool nodeShown(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
        if (s.tight[p] != 0) {
            return false;
        }
        if (s.yoga[p] != nullptr && YGNodeStyleGetDisplay(s.yoga[p]) == YGDisplayNone) {
            return false;
        }
    }
    return s.box[id].w >= 1.0f && s.box[id].h >= 1.0f;
}

bool clipAncestors(const UiState& s, uint16_t id, UiBox& out) {
    bool any = false;
    for (uint16_t p = s.parentOf[id]; p != kUiNone; p = s.parentOf[p]) {
        const UiBox& box = s.box[p];
        if (!any) {
            out = box;
            any = true;
            continue;
        }
        const float x0 = out.x > box.x ? out.x : box.x;
        const float y0 = out.y > box.y ? out.y : box.y;
        const float x1 = out.x + out.w < box.x + box.w ? out.x + out.w : box.x + box.w;
        const float y1 = out.y + out.h < box.y + box.h ? out.y + out.h : box.y + box.h;
        out.x = x0;
        out.y = y0;
        out.w = x1 > x0 ? x1 - x0 : 0.0f;
        out.h = y1 > y0 ? y1 - y0 : 0.0f;
    }
    return any;
}

uint32_t emitMark(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float w,
    float h,
    float r,
    float g,
    float b,
    float a,
    float radius,
    float spread,
    uint32_t flag,
    const UiBox* clip) {
    if (n >= cap || w < 0.5f || h < 0.5f || a <= 0.0f) {
        return n;
    }
    UiPrimitive& p = dst[n];
    p = {};
    p.posX = x;
    p.posY = y;
    p.sizeX = w;
    p.sizeY = h;
    p.color[0] = r;
    p.color[1] = g;
    p.color[2] = b;
    p.color[3] = a;
    p.extra[0] = radius;
    p.flags = flag;
    putFloat(p.pad1, spread);
    if (clip != nullptr) {
        p.flags |= kUiFlagClip;
        p.extra[1] = clip->x;
        p.extra[2] = clip->y;
        p.extra[3] = clip->w;
        putFloat(p.pad0, clip->h);
    }
    return n + 1;
}

struct UiGpuHeader {
    float extentX;
    float extentY;
    uint32_t count;
    uint32_t pad;
};

uint32_t emitPhoto(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox& box,
    const UiImage& image,
    const UiBox* clip) {
    if (n >= cap || box.w < 1.0f || box.h < 1.0f) {
        return n;
    }
    const float frames = image.frames == 0 ? 1.0f : static_cast<float>(image.frames);
    const float span = 1.0f / frames;
    const float base = static_cast<float>(image.frame) * span;
    UiPrimitive& p = dst[n];
    p = {};
    p.posX = box.x;
    p.posY = box.y;
    p.sizeX = box.w;
    p.sizeY = box.h;
    p.color[0] = 1.0f;
    p.color[1] = 1.0f;
    p.color[2] = 1.0f;
    p.color[3] = 1.0f;
    p.texId = image.slot;
    p.flags = kUiFlagPhoto;
    p.extra[0] = image.viewX;
    p.extra[1] = base + image.viewY * span;
    p.extra[2] = image.viewX + image.viewW;
    p.extra[3] = base + (image.viewY + image.viewH) * span;
    if (clip != nullptr) {
        p.flags |= kUiFlagClip;
        p.color[0] = clip->x;
        p.color[1] = clip->y;
        p.color[2] = clip->w;
        p.color[3] = clip->h;
    }
    ++n;
    if (image.minimap == 0 || box.w < 120.0f || box.h < 90.0f || n >= cap) {
        return n;
    }
    constexpr float kMapW = 72.0f;
    constexpr float kMapH = 48.0f;
    constexpr float kMargin = 8.0f;
    const float mx = box.x + box.w - kMargin - kMapW;
    const float my = box.y + box.h - kMargin - kMapH;
    n = emitRect(dst, n, cap, mx - 2.0f, my - 2.0f, kMapW + 4.0f, kMapH + 4.0f, 0.02f, 0.02f, 0.03f, 0.9f, 3.0f, clip);
    if (n >= cap) {
        return n;
    }
    UiPrimitive& map = dst[n++];
    map = {};
    map.posX = mx;
    map.posY = my;
    map.sizeX = kMapW;
    map.sizeY = kMapH;
    map.color[0] = 1.0f;
    map.color[1] = 1.0f;
    map.color[2] = 1.0f;
    map.color[3] = 1.0f;
    map.texId = image.slot;
    map.flags = kUiFlagPhoto;
    map.extra[0] = 0.0f;
    map.extra[1] = base;
    map.extra[2] = 1.0f;
    map.extra[3] = base + span;
    if (clip != nullptr) {
        map.flags |= kUiFlagClip;
        map.color[0] = clip->x;
        map.color[1] = clip->y;
        map.color[2] = clip->w;
        map.color[3] = clip->h;
    }
    const float vx = mx + image.viewX * kMapW;
    const float vy = my + image.viewY * kMapH;
    const float vw = image.viewW * kMapW;
    const float vh = image.viewH * kMapH;
    n = emitRect(dst, n, cap, vx, vy, vw < 2.0f ? 2.0f : vw, 2.0f, 1.0f, 1.0f, 1.0f, 0.95f, 0.0f, clip);
    n = emitRect(dst, n, cap, vx, vy + (vh > 2.0f ? vh - 2.0f : 0.0f), vw < 2.0f ? 2.0f : vw, 2.0f, 1.0f, 1.0f, 1.0f, 0.95f, 0.0f, clip);
    n = emitRect(dst, n, cap, vx, vy, 2.0f, vh < 2.0f ? 2.0f : vh, 1.0f, 1.0f, 1.0f, 0.95f, 0.0f, clip);
    n = emitRect(dst, n, cap, vx + (vw > 2.0f ? vw - 2.0f : 0.0f), vy, 2.0f, vh < 2.0f ? 2.0f : vh, 1.0f, 1.0f, 1.0f, 0.95f, 0.0f, clip);
    return n;
}

void boost(float& r, float& g, float& b) {
    r = r + 0.08f > 1.0f ? 1.0f : r + 0.08f;
    g = g + 0.08f > 1.0f ? 1.0f : g + 0.08f;
    b = b + 0.08f > 1.0f ? 1.0f : b + 0.08f;
}

} // namespace

uint32_t uiEmitText(
    UiState& s,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const char* text,
    uint8_t len,
    const UiBox& box,
    float r,
    float g,
    float b,
    const UiBox* clip,
    bool center) {
    return emitGlyphs(dst, n, cap, text, len, s, box, r, g, b, clip, center, 1.0f, 0.0f, 0.0f, 0.0f);
}

uint32_t uiEmitRun(
    UiState& s,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const TextRun& run,
    float r,
    float g,
    float b,
    float a,
    const UiBox* clip) {
    for (uint8_t i = 0; i < run.lines; ++i) {
        const TextLine& line = run.line[i];
        if (line.len == 0 || line.h < 0.5f) {
            continue;
        }
        const UiBox box{line.x, line.y, line.w + 1.0f, line.h};
        const float spaceGap = run.wordGap + line.gap;
        if (run.shadowX != 0.0f || run.shadowY != 0.0f) {
            const UiBox shade{line.x + run.shadowX, line.y + run.shadowY, line.w + 1.0f, line.h};
            n = emitGlyphs(dst, n, cap, run.bytes + line.begin, line.len, s, shade, 0.0f, 0.0f, 0.0f, clip, false, a * 0.45f, run.tracking, spaceGap, run.scale);
        }
        n = emitGlyphs(dst, n, cap, run.bytes + line.begin, line.len, s, box, r, g, b, clip, false, a, run.tracking, spaceGap, run.scale);
        const float thick = run.thick > 0.4f ? run.thick : 1.0f;
        if ((run.deco & 1) != 0 && line.w > 1.0f) {
            n = emitRect(dst, n, cap, line.x, line.y + line.h - thick - run.under, line.w, thick, r, g, b, a, 0.0f, clip);
        }
        if ((run.deco & 2) != 0 && line.w > 1.0f) {
            n = emitRect(dst, n, cap, line.x, line.y + line.h * 0.45f, line.w, thick, r, g, b, a, 0.0f, clip);
        }
        if ((run.deco & 4) != 0 && line.w > 1.0f) {
            n = emitRect(dst, n, cap, line.x, line.y + 1.0f, line.w, thick, r, g, b, a, 0.0f, clip);
        }
    }
    return n;
}

void uiLayout(UiState& s, float w, float h) {
    if (!s.layoutDirty && s.laidW == w && s.laidH == h) {
        return;
    }
    if (s.count == 0) {
        return;
    }
    auto measure = [&]() {
        for (uint16_t id = 0; id < s.count; ++id) {
            const UiFlex* flex = s.ent[id].try_get<UiFlex>();
            const UiPaint* paint = s.ent[id].try_get<UiPaint>();
            if (flex == nullptr || s.yoga[id] == nullptr) {
                continue;
            }
            applyFlex(s.yoga[id], *flex);
            if (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Scroll)) {
                YGNodeStyleSetOverflow(s.yoga[id], YGOverflowHidden);
            }
        }
        YGNodeStyleSetWidth(s.yoga[0], w);
        YGNodeStyleSetHeight(s.yoga[0], h);
        YGNodeCalculateLayout(s.yoga[0], w, h, YGDirectionLTR);
        for (uint16_t id = 0; id < s.count; ++id) {
            cacheBox(s, id);
        }
    };
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.culled[id] != 0 && s.yoga[id] != nullptr) {
            YGNodeStyleSetDisplay(s.yoga[id], YGDisplayFlex);
            s.culled[id] = 0;
        }
    }
    measure();
    for (int pass = 0; pass < 8; ++pass) {
        uint16_t victim = kUiNone;
        for (uint16_t parent = 0; parent < s.count; ++parent) {
            const UiFlex* flex = s.ent[parent].try_get<UiFlex>();
            if (flex == nullptr || flex->drop == 0 || s.yoga[parent] == nullptr) {
                continue;
            }
            if (YGNodeStyleGetDisplay(s.yoga[parent]) == YGDisplayNone) {
                continue;
            }
            uint16_t kids[32];
            int nk = 0;
            for (uint16_t id = 0; id < s.count && nk < 32; ++id) {
                if (s.parentOf[id] != parent || s.yoga[id] == nullptr) {
                    continue;
                }
                if (YGNodeStyleGetDisplay(s.yoga[id]) == YGDisplayNone) {
                    continue;
                }
                kids[nk++] = id;
            }
            bool tight = false;
            for (int i = 0; i < nk; ++i) {
                const UiFlex* child = s.ent[kids[i]].try_get<UiFlex>();
                if (child == nullptr) {
                    continue;
                }
                if ((child->minW > 1.0f && s.box[kids[i]].w + 0.5f < child->minW)
                    || (child->minH > 1.0f && s.box[kids[i]].h + 0.5f < child->minH)) {
                    tight = true;
                }
            }
            if (!tight || nk < 2) {
                continue;
            }
            victim = kids[flex->drop == 2 ? 0 : nk - 1];
            break;
        }
        if (victim == kUiNone) {
            break;
        }
        YGNodeStyleSetDisplay(s.yoga[victim], YGDisplayNone);
        s.culled[victim] = 1;
        measure();
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        s.tight[id] = 0;
        if (s.culled[id] != 0) {
            continue;
        }
        const UiFlex* flex = s.ent[id].try_get<UiFlex>();
        if (flex == nullptr) {
            continue;
        }
        if ((flex->minW > 1.0f && s.box[id].w + 0.5f < flex->minW)
            || (flex->minH > 1.0f && s.box[id].h + 0.5f < flex->minH)) {
            s.tight[id] = 1;
        }
    }
    s.laidW = w;
    s.laidH = h;
    s.layoutDirty = false;
    s.visualDirty = true;
    s.visualDirty = true;
}

uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap) {
    uint16_t ids[kUiCap];
    uint16_t nIds = 0;
    s.paints.each([&](flecs::entity, UiSlot& slot, UiPaint&) {
        if (nIds < kUiCap) {
            ids[nIds++] = slot.index;
        }
    });
    for (uint16_t a = 1; a < nIds; ++a) {
        const uint16_t value = ids[a];
        uint16_t b = a;
        while (b > 0 && ids[b - 1] > value) {
            ids[b] = ids[b - 1];
            --b;
        }
        ids[b] = value;
    }

    fieldPlaceText(s);
    placeChromeText(s);
    uint32_t n = 0;
    for (int pass = 0; pass < 2; ++pass) {
    for (uint16_t k = 0; k < nIds; ++k) {
        const uint16_t id = ids[k];
        if (uiIsOverlay(s, id) != (pass == 1)) {
            continue;
        }
        const UiPaint* paintPtr = s.ent[id].try_get<UiPaint>();
        if (paintPtr == nullptr || id >= s.count) {
            continue;
        }
        const UiPaint& paint = *paintPtr;
        const auto role = static_cast<UiRole>(paint.role);
        if (role == UiRole::Hidden || !nodeShown(s, id)) {
            continue;
        }
        const UiBox& box = s.box[id];
        UiBox ancestor{};
        const bool hasClip = clipAncestors(s, id, ancestor);
        const UiBox* clip = hasClip ? &ancestor : nullptr;
        float r = paint.r;
        float g = paint.g;
        float b = paint.b;
        float a = paint.a;
        if (role == UiRole::Button && paint.tag == s.selected && paint.tag >= 0 && paint.tag < 3) {
            r = s.look.accent[paint.tag][0];
            g = s.look.accent[paint.tag][1];
            b = s.look.accent[paint.tag][2];
        }
        if (s.anim[id].on != 0 && s.anim[id].dur > 0.0f) {
            float u = (s.time - s.anim[id].t0) / s.anim[id].dur;
            if (u < 0.0f) {
                u = 0.0f;
            }
            if (u > 1.0f) {
                u = 1.0f;
            }
            u = u * u * (3.0f - 2.0f * u);
            r = s.anim[id].fr + (s.anim[id].tr - s.anim[id].fr) * u;
            g = s.anim[id].fg + (s.anim[id].tg - s.anim[id].fg) * u;
            b = s.anim[id].fb + (s.anim[id].tb - s.anim[id].fb) * u;
            a = s.anim[id].fa + (s.anim[id].ta - s.anim[id].fa) * u;
        }
        const bool hot = paint.disabled == 0 && id == s.hovered
            && (role == UiRole::Button || role == UiRole::Close || role == UiRole::Check || role == UiRole::Field
                || role == UiRole::Slider);
        if (paint.disabled != 0) {
            a *= 0.45f;
        } else if (hot && paint.hr >= 0.0f) {
            r = paint.hr;
            g = paint.hg;
            b = paint.hb;
        } else if (hot || id == s.focused || id == s.dragId) {
            boost(r, g, b);
        }

        if (s.plug.draw != nullptr && s.plug.draw(s, id, dst, n, cap)) {
            continue;
        }
        if (paint.tag == -90) {
            const UiImage* image = s.ent[id].try_get<UiImage>();
            if (image != nullptr) {
                n = emitPhoto(dst, n, cap, box, *image, clip);
            }
            continue;
        }
        if (paint.tag == -70) {
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, 1.0f, 1.0f, 1.0f, 0, kUiFlagWheel);
            const float radius = std::min(box.w, box.h) * 0.5f;
            const float ang = (s.colorH - 0.5f) * 6.2831853f;
            const float mx = box.x + box.w * 0.5f + std::cos(ang) * s.colorS * radius;
            const float my = box.y + box.h * 0.5f + std::sin(ang) * s.colorS * radius;
            n = emitRect(dst, n, cap, mx - 7.0f, my - 7.0f, 14.0f, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f, 7.0f, nullptr);
            n = emitRect(dst, n, cap, mx - 4.0f, my - 4.0f, 8.0f, 8.0f, s.colorR, s.colorG, s.colorB, 1.0f, 4.0f, nullptr);
            continue;
        }
        if (paint.tag == -71) {
            float pr = 0.0f;
            float pg = 0.0f;
            float pb = 0.0f;
            const float hh = s.colorH * 6.0f;
            const int hi = static_cast<int>(hh) % 6;
            const float f = hh - static_cast<float>(static_cast<int>(hh));
            const float pv = 1.0f;
            const float q = pv * (1.0f - f * s.colorS);
            const float t = pv * (1.0f - (1.0f - f) * s.colorS);
            const float pp = pv * (1.0f - s.colorS);
            if (hi == 0) { pr = pv; pg = t; pb = pp; }
            else if (hi == 1) { pr = q; pg = pv; pb = pp; }
            else if (hi == 2) { pr = pp; pg = pv; pb = t; }
            else if (hi == 3) { pr = pp; pg = q; pb = pv; }
            else if (hi == 4) { pr = t; pg = pp; pb = pv; }
            else { pr = pv; pg = pp; pb = q; }
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, pr, pg, pb, 1, kUiFlagRamp);
            const float ty = box.y + (1.0f - s.colorV) * box.h;
            n = emitRect(dst, n, cap, box.x - 2.0f, ty - 3.0f, box.w + 4.0f, 6.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, nullptr);
            continue;
        }
        if (paint.tag == -78) {
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, 0.0f, 0.0f, 0.0f, 6, kUiFlagRamp);
            n = emitRect(dst, n, cap, box.x, box.y, box.w, box.h, s.colorR, s.colorG, s.colorB, s.colorA, paint.radius, nullptr);
            continue;
        }
        if (paint.tag == -72) {
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, s.colorR, s.colorG, s.colorB, 2, kUiFlagRamp);
            const float tx = box.x + s.colorA * box.w;
            n = emitRect(dst, n, cap, tx - 3.0f, box.y - 2.0f, 6.0f, box.h + 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, nullptr);
            continue;
        }
        if (role == UiRole::Label) {
            const UiText* text = s.ent[id].try_get<UiText>();
            if (text != nullptr) {
                UiBox limit = box;
                const uint16_t parentId = s.parentOf[id];
                const UiField* host = nullptr;
                if (parentId != kUiNone) {
                    const UiPaint* parentPaint = s.ent[parentId].try_get<UiPaint>();
                    if (parentPaint != nullptr && parentPaint->role == static_cast<uint8_t>(UiRole::Field)) {
                        host = s.ent[parentId].try_get<UiField>();
                    }
                }
                if (host != nullptr) {
                    limit = fieldInset(s, parentId, *host);
                } else if (parentId != kUiNone) {
                    const UiBox& parent = s.box[parentId];
                    const float x0 = limit.x > parent.x ? limit.x : parent.x;
                    const float y0 = limit.y > parent.y ? limit.y : parent.y;
                    const float x1 = limit.x + limit.w < parent.x + parent.w ? limit.x + limit.w : parent.x + parent.w;
                    const float y1 = limit.y + limit.h < parent.y + parent.h ? limit.y + limit.h : parent.y + parent.h;
                    limit.x = x0;
                    limit.y = y0;
                    limit.w = x1 > x0 ? x1 - x0 : 0.0f;
                    limit.h = y1 > y0 ? y1 - y0 : 0.0f;
                }
                if (hasClip) {
                    const float x0 = limit.x > ancestor.x ? limit.x : ancestor.x;
                    const float y0 = limit.y > ancestor.y ? limit.y : ancestor.y;
                    const float x1 = limit.x + limit.w < ancestor.x + ancestor.w ? limit.x + limit.w : ancestor.x + ancestor.w;
                    const float y1 = limit.y + limit.h < ancestor.y + ancestor.h ? limit.y + limit.h : ancestor.y + ancestor.h;
                    limit.x = x0;
                    limit.y = y0;
                    limit.w = x1 > x0 ? x1 - x0 : 0.0f;
                    limit.h = y1 > y0 ? y1 - y0 : 0.0f;
                }
                const TextRun run = textRun(s, *text, box);
                if (run.lines > 0) {
                    n = uiEmitRun(s, dst, n, cap, run, r, g, b, a, &limit);
                }
            }
            continue;
        }
        if (role != UiRole::Label) {
            float br = r;
            float bg = g;
            float bb = b;
            float ba = a;
            if (paint.tag == -5) {
                const bool bar = id == s.hovered || id == s.capture;
                if (bar) {
                    n = emitRect(
                        dst, n, cap, box.x + (box.w - 2.0f) * 0.5f, box.y, 2.0f, box.h,
                        s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.95f, 0.0f, clip);
                }
                continue;
            } else if (hot && role == UiRole::Button && a < 0.05f) {
                br = 0.36f;
                bg = 0.48f;
                bb = 0.72f;
                ba = 0.40f;
            }
            const UiField* need = role == UiRole::Field ? s.ent[id].try_get<UiField>() : nullptr;
            const bool missing = need != nullptr && need->required != 0 && need->len == 0;
            const bool focusRing = paint.disabled == 0 && id == s.focused
                && (role == UiRole::Field || role == UiRole::Check || role == UiRole::Button);
            const float ring = paint.borderW > 0.4f || missing ? (paint.borderW > 0.4f ? paint.borderW : 1.5f) : (focusRing ? 1.5f : 0.0f);
            if (paint.shadow > 0.5f && ba > 0.02f) {
                const float sh = paint.shadow;
                n = emitMark(
                    dst, n, cap, box.x - sh, box.y - sh * 0.15f, box.w + sh * 2.0f, box.h + sh * 2.0f,
                    0.0f, 0.0f, 0.0f, 0.42f, paint.radius + sh, sh, kUiFlagShadow, clip);
            }
            n = emitRect(dst, n, cap, box.x, box.y, box.w, box.h, br, bg, bb, ba, paint.radius, clip);
            if (ring > 0.4f) {
                const float orr = missing ? s.look.accent[2][0] : (focusRing && paint.borderW < 0.4f ? s.look.accent[0][0] : paint.br);
                const float org = missing ? s.look.accent[2][1] : (focusRing && paint.borderW < 0.4f ? s.look.accent[0][1] : paint.bg);
                const float orb = missing ? s.look.accent[2][2] : (focusRing && paint.borderW < 0.4f ? s.look.accent[0][2] : paint.bb);
                n = emitMark(dst, n, cap, box.x, box.y, box.w, box.h, orr, org, orb, ba > 0.2f ? ba : 1.0f, paint.radius, ring, kUiFlagStroke, clip);
            }
            if (role == UiRole::Progress) {
                const UiRange* range = s.ent[id].try_get<UiRange>();
                float t = 0.0f;
                if (range != nullptr && range->max > range->min) {
                    t = (range->value - range->min) / (range->max - range->min);
                }
                if (t < 0.0f) {
                    t = 0.0f;
                }
                if (t > 1.0f) {
                    t = 1.0f;
                }
                n = emitRect(
                    dst, n, cap, box.x, box.y, box.w * t, box.h,
                    s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, paint.radius, clip);
            } else if (role == UiRole::Scroll) {
                UiBox thumb{};
                if (scrollThumb(s, id, thumb)) {
                    n = emitRect(dst, n, cap, thumb.x, thumb.y, thumb.w, thumb.h, 0.55f, 0.60f, 0.70f, 0.9f, 3.0f, clip);
                }
            }
        }
        if ((role == UiRole::Button || role == UiRole::Close) && paint.icon != 0 && n < cap) {
            const UiText* host = s.ent[id].try_get<UiText>();
            const bool hasText = host != nullptr && host->len > 0;
            float ir = 0.93f;
            float ig = 0.94f;
            float ib = 0.96f;
            if (a > 0.2f) {
                textInk(r, g, b, hot, ir, ig, ib);
            }
            constexpr float kSide = 14.0f;
            UiPrimitive& icon = dst[n++];
            icon = {};
            icon.posX = hasText ? box.x + 6.0f : box.x + (box.w - kSide) * 0.5f;
            icon.posY = box.y + (box.h - kSide) * 0.5f;
            icon.sizeX = kSide;
            icon.sizeY = kSide;
            icon.color[0] = ir;
            icon.color[1] = ig;
            icon.color[2] = ib;
            icon.color[3] = 1.0f;
            icon.texId = paint.icon;
            icon.flags = kUiFlagIcon;
        }
        if (role == UiRole::Slider) {
            const UiRange* range = s.ent[id].try_get<UiRange>();
            float t = 0.0f;
            if (range != nullptr && range->max > range->min) {
                t = (range->value - range->min) / (range->max - range->min);
            }
            if (t < 0.0f) {
                t = 0.0f;
            }
            if (t > 1.0f) {
                t = 1.0f;
            }
            const float fillW = box.w * t;
            if (paint.ramp == 4) {
                n = emitRamp(dst, n, cap, box.x, box.y + box.h * 0.25f, box.w, box.h * 0.50f, 1.0f, 1.0f, 1.0f, 3, kUiFlagRamp);
            } else if (paint.ramp == 5 || paint.ramp == 6) {
                n = emitRamp(dst, n, cap, box.x, box.y + box.h * 0.25f, box.w, box.h * 0.50f, paint.r, paint.g, paint.b, paint.ramp == 5 ? 4u : 5u, kUiFlagRamp);
            } else if (paint.ramp >= 1 && paint.ramp <= 3) {
                const float cr = paint.ramp == 1 ? 1.0f : 0.0f;
                const float cg = paint.ramp == 2 ? 1.0f : 0.0f;
                const float cb = paint.ramp == 3 ? 1.0f : 0.0f;
                n = emitRamp(dst, n, cap, box.x, box.y + box.h * 0.25f, box.w, box.h * 0.50f, cr, cg, cb, 5, kUiFlagRamp);
            } else if (paint.ramp == 7) {
                n = emitRamp(dst, n, cap, box.x, box.y + box.h * 0.25f, box.w, box.h * 0.50f, paint.r, paint.g, paint.b, 2, kUiFlagRamp);
            } else {
                n = emitRect(dst, n, cap, box.x, box.y + box.h * 0.35f, fillW, box.h * 0.30f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, 4.0f, clip);
            }
            n = emitRect(dst, n, cap, box.x + fillW - 6.0f, box.y + 3.0f, 12.0f, box.h - 6.0f, 0.93f, 0.94f, 0.96f, 1.0f, 6.0f, clip);
        } else if (role == UiRole::Check) {
            const UiCheck* check = s.ent[id].try_get<UiCheck>();
            const bool radio = check != nullptr && check->kind == 1;
            const float side = 18.0f;
            const float by = box.y + (box.h - side) * 0.5f;
            const float rad = radio ? side * 0.5f : 4.0f;
            n = emitRect(dst, n, cap, box.x + 8.0f, by, side, side, 0.10f, 0.11f, 0.14f, 1.0f, rad, clip);
            if (check != nullptr && check->on != 0) {
                if (radio) {
                    n = emitRect(dst, n, cap, box.x + 13.0f, by + 5.0f, side - 10.0f, side - 10.0f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, (side - 10.0f) * 0.5f, clip);
                } else {
                    n = emitRect(dst, n, cap, box.x + 12.0f, by + 4.0f, side - 8.0f, side - 8.0f, s.look.accent[1][0], s.look.accent[1][1], s.look.accent[1][2], 1.0f, 2.0f, clip);
                }
            }
        } else if (role == UiRole::Field) {
            const UiField* field = s.ent[id].try_get<UiField>();
            if (field != nullptr) {
                const UiBox inset = fieldInset(s, id, *field);
                const uint8_t sel0 = field->anchor < field->caret ? field->anchor : field->caret;
                const uint8_t sel1 = field->anchor < field->caret ? field->caret : field->anchor;
                if (sel0 != sel1 && field->len > 0) {
                    const float x0 = inset.x + uiFieldWidth(s, *field, sel0) - field->scroll;
                    const float x1 = inset.x + uiFieldWidth(s, *field, sel1) - field->scroll;
                    const float left = x0 > inset.x ? x0 : inset.x;
                    const float right = x1 < inset.x + inset.w ? x1 : inset.x + inset.w;
                    if (right > left) {
                        n = emitRect(dst, n, cap, left, box.y + 6.0f, right - left, box.h - 12.0f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.45f, 2.0f, clip);
                    }
                }
                if (id == s.focused && paint.disabled == 0) {
                    const float cx = inset.x + uiFieldWidth(s, *field, field->caret) - field->scroll;
                    if (cx >= inset.x - 1.0f && cx <= inset.x + inset.w) {
                        n = emitRect(dst, n, cap, cx, box.y + 7.0f, 1.5f, box.h - 14.0f, field->text[0], field->text[1], field->text[2], 1.0f, 0.0f, clip);
                    }
                }
                const float clearW = field->clear != 0 && field->len > 0 ? 22.0f : 0.0f;
                if (clearW > 0.0f && n < cap) {
                    constexpr float kSide = 12.0f;
                    UiPrimitive& icon = dst[n++];
                    icon = {};
                    icon.posX = box.x + box.w - field->padR - kSide;
                    icon.posY = box.y + (box.h - kSide) * 0.5f;
                    icon.sizeX = kSide;
                    icon.sizeY = kSide;
                    icon.color[0] = 0.75f;
                    icon.color[1] = 0.78f;
                    icon.color[2] = 0.84f;
                    icon.color[3] = 1.0f;
                    icon.texId = 3;
                    icon.flags = kUiFlagIcon;
                }
            }
        }
    }
    }
    if (s.hintOn != 0) {
        n = emitRect(dst, n, cap, s.hint.x, s.hint.y, s.hint.w, s.hint.h, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.35f, 4.0f, nullptr);
    }
    if (s.edgeOn != 0) {
        n = emitRect(dst, n, cap, s.edgeLine.x, s.edgeLine.y, s.edgeLine.w, s.edgeLine.h, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.95f, 0.0f, nullptr);
    }
    return n;
}

bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps) {
    canvasDestroy(c, d);
    c.state = new UiState();
    c.nullMesh = d.caps.meshShader;

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(kUiFontBase) + kUiFontBytes;
    if (!gpuBufferCreate(
            c.prims,
            d,
            bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true)) {
        spdlog::error("ui primitive buffer failed");
        canvasDestroy(c, d);
        return false;
    }
    c.device = &d;
    c.heaps = &heaps;
    if (!canvasSetFont(c, "/usr/share/fonts/TTF/DejaVuSans.ttf", 32.0f)) {
        spdlog::error("default sdf font failed");
        canvasDestroy(c, d);
        return false;
    }

    if (!heapWriteBuffer(heaps, d, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c.prims.address, c.prims.size)) {
        spdlog::error("ui heap write failed");
        canvasDestroy(c, d);
        return false;
    }

    uint8_t blank[4] = {28, 30, 36, 255};
    std::vector<uint8_t> demo(128u * 256u * 4u);
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 128; ++x) {
            uint8_t* px = demo.data() + (static_cast<size_t>(y) * 128u + x) * 4u;
            const bool second = y >= 128;
            const uint32_t ly = second ? y - 128 : y;
            px[0] = static_cast<uint8_t>(x * 2);
            px[1] = static_cast<uint8_t>(ly * 2);
            px[2] = second ? 210 : 48;
            px[3] = 255;
        }
    }
    bool photosOk = gpuImageUploadRgba(c.photos[0], d, demo.data(), 128, 256, 2);
    c.photoMeta[0].frames = 2;
    c.photoMeta[0].delayMs[0] = 420;
    c.photoMeta[0].delayMs[1] = 420;
    for (uint8_t slot = 1; slot < kPhotoSlots && photosOk; ++slot) {
        photosOk = gpuImageUploadRgba(c.photos[slot], d, blank, 1, 1);
        c.photoMeta[slot].frames = 1;
        c.photoMeta[slot].delayMs[0] = 100;
    }
    for (uint8_t slot = 0; slot < kPhotoSlots && photosOk; ++slot) {
        photosOk = heapWriteImage(
            heaps,
            d,
            static_cast<uint32_t>(slot) + 1u,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            c.photos[slot],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    if (!photosOk) {
        spdlog::error("ui photo slots failed");
        canvasDestroy(c, d);
        return false;
    }

    VkDescriptorSetAndBindingMappingEXT vsMap = heapMap(
        0,
        0,
        VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(heaps, 0),
        static_cast<uint32_t>(heaps.bufferDescSize));
    VkDescriptorSetAndBindingMappingEXT fsMap[3];
    fsMap[0] = heapMap(
        0,
        1,
        VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(heaps, 0),
        static_cast<uint32_t>(heaps.imageDescSize));
    fsMap[1] = heapMap(
        1,
        0,
        VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(heaps, 0),
        static_cast<uint32_t>(heaps.samplerDescSize));
    fsMap[2] = heapMap(
        0,
        2,
        VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(heaps, 1),
        static_cast<uint32_t>(heaps.imageDescSize));
    const ShaderCreateDesc vs{
        .path = BH_UI_VERT,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 1,
        .mappings = &vsMap,
    };
    const ShaderCreateDesc fs{
        .path = BH_UI_FRAG,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 3,
        .mappings = fsMap,
    };
    if (!shaderCreate(d, vs, c.vs) || !shaderCreate(d, fs, c.fs)) {
        spdlog::error("ui shaders failed");
        canvasDestroy(c, d);
        return false;
    }
    return true;
}

void canvasDestroy(Canvas& c, Device& d) {
    shaderDestroy(d, c.vs);
    shaderDestroy(d, c.fs);
    gpuImageDestroy(c.font, d);
    for (uint8_t slot = 0; slot < kPhotoSlots; ++slot) {
        gpuImageDestroy(c.photos[slot], d);
        c.photoMeta[slot] = {};
    }
    gpuBufferDestroy(c.prims, d);
    if (c.state != nullptr) {
        jsonRelease(*c.state);
        if (c.state->plug.shutdown != nullptr) {
            c.state->plug.shutdown(*c.state);
        }
        if (c.state->count > 0 && c.state->yoga[0] != nullptr) {
            YGNodeFreeRecursive(c.state->yoga[0]);
        }
        delete c.state;
        c.state = nullptr;
    }
    c.drawCount = 0;
    c.nullMesh = false;
}

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

void canvasSetLook(Canvas& c, const UiLook& look) {
    if (c.state == nullptr) {
        return;
    }
    const float glyphH = c.state->look.glyphH;
    const float glyphW = c.state->look.glyphW;
    const float advance = c.state->look.advance;
    const float title = c.state->look.titleBar;
    const float border = c.state->look.resizeBorder;
    c.state->look = look;
    c.state->look.glyphH = glyphH;
    c.state->look.glyphW = glyphW;
    c.state->look.advance = advance;
    c.state->look.titleBar = title;
    c.state->look.resizeBorder = border;
    uiRestyle(*c.state);
}

UiLook canvasLook(const Canvas& c) {
    if (c.state == nullptr) {
        return {};
    }
    return c.state->look;
}

void canvasBuildSample(Canvas& c) {
    if (c.state != nullptr) {
        uiBuildShell(*c.state);
    }
}

void canvasBuildColor(Canvas& c) {
    if (c.state != nullptr) {
        uiBuildColor(*c.state);
    }
}

void canvasSetBook(Canvas& c, ColorBook* book) {
    if (c.state != nullptr) {
        c.state->book = book;
    }
}

uint16_t canvasImportJson(Canvas& c, uint16_t parent, const char* text) {
    if (c.state == nullptr) {
        return kUiNone;
    }
    return uiImportJson(*c.state, parent, text);
}

uint16_t canvasFind(const Canvas& c, const char* name) {
    if (c.state == nullptr) {
        return kUiNone;
    }
    return uiFindName(*c.state, name);
}

void canvasSetBuilder(Canvas& c, UiBuilder builder, void* user) {
    c.builder = builder;
    c.builderUser = user;
}

void photoPush(Canvas& c, uint8_t slot) {
    if (c.state == nullptr || slot >= kPhotoSlots || c.state->photoView == kUiNone || c.state->photoView >= c.state->count) {
        return;
    }
    auto* image = c.state->ent[c.state->photoView].try_get_mut<UiImage>();
    if (image == nullptr || image->slot != slot) {
        return;
    }
    const PhotoMeta& meta = c.photoMeta[slot];
    image->frames = meta.frames == 0 ? 1 : meta.frames;
    if (image->frame >= image->frames) {
        image->frame = 0;
    }
    for (uint32_t i = 0; i < kImageFrames; ++i) {
        image->delayMs[i] = meta.delayMs[i];
    }
    c.state->visualDirty = true;
}

void canvasReload(Canvas& c) {
    if (c.state == nullptr) {
        return;
    }
    uiClear(*c.state);
    if (c.builder != nullptr) {
        c.builder(c, c.builderUser);
    }
    photoPush(c, 0);
    c.state->layoutDirty = true;
    c.state->visualDirty = true;
    spdlog::info("ui: {} flecs nodes, yoga + 1 draw", c.state->count);
}

void canvasShow(Canvas& c, uint16_t id, bool visible) {
    if (c.state != nullptr) {
        uiShow(*c.state, id, visible);
    }
}

void canvasBind(Canvas& c, uint16_t id, UiClickFn fn, void* user) {
    if (c.state != nullptr) {
        uiBind(*c.state, id, fn, user);
    }
}

void canvasFold(Canvas& c, uint16_t id) {
    if (c.state != nullptr) {
        uiFold(*c.state, id);
    }
}

void canvasOnFocus(Canvas& c, UiFocusFn fn, void* user) {
    if (c.state != nullptr) {
        c.state->focusFn = fn;
        c.state->focusUser = user;
    }
}

void canvasSetClick(Canvas& c, UiClickFn fn, void* user) {
    if (c.state != nullptr) {
        c.state->onClick = fn;
        c.state->clickUser = user;
    }
}

bool canvasListen(Canvas& c, UiValueFn fn, void* user) {
    return c.state != nullptr && uiListen(*c.state, fn, user);
}

bool canvasRange(const Canvas& c, uint16_t id, float& value, float& minV, float& maxV) {
    if (c.state == nullptr || id >= c.state->count) {
        return false;
    }
    const UiRange* range = c.state->ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    value = range->value;
    minV = range->min;
    maxV = range->max;
    return true;
}

void canvasSetHeight(Canvas& c, uint16_t id, float h) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* flex = c.state->ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->heightMode = static_cast<uint8_t>(UiSize::Px);
    flex->height = h;
    c.state->layoutDirty = true;
    c.state->visualDirty = true;
}

void canvasReparent(Canvas& c, uint16_t id, uint16_t parent) {
    if (c.state != nullptr) {
        uiReparent(*c.state, id, parent);
    }
}

void canvasPlace(Canvas& c, uint16_t id, bool absolute, float x, float y, float w, float h) {
    if (c.state != nullptr) {
        uiPlace(*c.state, id, absolute, x, y, w, h);
    }
}

void canvasGrow(Canvas& c, uint16_t id, float extraW, float h) {
    if (c.state != nullptr) {
        uiGrow(*c.state, id, extraW, h);
    }
}

void canvasDock(Canvas& c, uint16_t panel, uint16_t home, uint8_t edges) {
    if (c.state == nullptr) {
        return;
    }
    c.state->dockPanel = panel;
    c.state->dockHome = home;
    c.state->dockEdges = edges;
}

void canvasCommitDock(Canvas& c, uint8_t edge) {
    if (c.state != nullptr) {
        uiDockTo(*c.state, edge);
    }
}

void canvasSetDrop(Canvas& c, uint16_t id, uint8_t drop) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* flex = c.state->ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->drop = drop;
        c.state->layoutDirty = true;
    }
}

void canvasOpenOs(Canvas& c) {
    if (c.state != nullptr) {
        c.state->opTool = true;
    }
}

void canvasOfferFile(Canvas& c, const char* path) {
    if (c.state == nullptr || path == nullptr || path[0] == '\0' || c.state->fileTarget == kUiNone) {
        return;
    }
    uiSetFieldText(*c.state, c.state->fileTarget, path);
}

bool canvasLoadImage(Canvas& c, uint8_t slot, const char* path) {
    if (slot >= kPhotoSlots || c.device == nullptr || c.heaps == nullptr || path == nullptr) {
        return false;
    }
    ImageCpu cpu{};
    if (!imageLoadFile(cpu, path)) {
        return false;
    }
    const uint32_t stacked = cpu.height * (cpu.frames == 0 ? 1u : cpu.frames);
    bool ok = cpu.hdr != 0
        ? gpuImageUploadHdr(c.photos[slot], *c.device, cpu.rgb, cpu.width, cpu.height)
        : gpuImageUploadRgba(c.photos[slot], *c.device, cpu.rgba, cpu.width, stacked, cpu.frames == 0 ? 1u : cpu.frames);
    if (ok) {
        ok = heapWriteImage(
            *c.heaps,
            *c.device,
            static_cast<uint32_t>(slot) + 1u,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            c.photos[slot],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    if (ok) {
        c.photoMeta[slot].frames = static_cast<uint8_t>(cpu.frames == 0 ? 1u : cpu.frames);
        for (uint32_t i = 0; i < kImageFrames; ++i) {
            c.photoMeta[slot].delayMs[i] = cpu.delayMs[i];
        }
        photoPush(c, slot);
        spdlog::info("image slot {} {}x{} x{} frames", slot, cpu.width, cpu.height, cpu.frames);
    }
    imageCpuFree(cpu);
    return ok;
}

uint32_t canvasLoadJsonDoc(Canvas& c, const char* path) {
    if (c.state == nullptr) {
        return 0;
    }
    return jsonLoadFile(*c.state, path);
}

bool canvasSaveJsonDoc(const Canvas& c, const char* path) {
    if (c.state == nullptr) {
        return false;
    }
    return jsonSaveFile(*c.state, path);
}

int canvasCursor(const Canvas& c) {
    return c.state != nullptr ? static_cast<int>(c.state->cursor) : 0;
}

void canvasSetIcon(Canvas& c, uint16_t id, uint8_t icon) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* paint = c.state->ent[id].try_get_mut<UiPaint>();
    if (paint != nullptr) {
        paint->icon = icon;
        c.state->visualDirty = true;
    }
}

void canvasSelection(const Canvas& c, uint16_t& id, char* name, uint8_t cap, uint8_t& len) {
    id = kUiNone;
    len = 0;
    if (name != nullptr && cap > 0) {
        name[0] = '\0';
    }
    if (c.state == nullptr) {
        return;
    }
    id = c.state->pickedId;
    const uint8_t n = c.state->pickedLen < cap ? c.state->pickedLen : static_cast<uint8_t>(cap > 0 ? cap - 1 : 0);
    if (name != nullptr) {
        for (uint8_t i = 0; i < n; ++i) {
            name[i] = c.state->pickedName[i];
        }
        if (cap > 0) {
            name[n] = '\0';
        }
    }
    len = n;
}

void canvasBindContext(Canvas& c, uint16_t owner, uint16_t menu) {
    if (c.state != nullptr && owner < kUiCap) {
        c.state->contextOf[owner] = menu;
    }
}

uint16_t canvasMakeMenu(Canvas& c) {
    if (c.state == nullptr || c.state->count == 0) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.position = 1;
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = c.state->menuW;
    flex.pad = c.state->menuPad;
    flex.gap = c.state->menuGap;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Panel);
    paint.token = static_cast<uint8_t>(UiToken::Menu);
    paint.r = c.state->look.menu[0];
    paint.g = c.state->look.menu[1];
    paint.b = c.state->look.menu[2];
    paint.a = 1.0f;
    paint.radius = c.state->look.radiusSm;
    const uint16_t id = uiNode(*c.state, 0, flex, paint);
    if (id != kUiNone && c.state->yoga[id] != nullptr) {
        YGNodeStyleSetDisplay(c.state->yoga[id], YGDisplayNone);
    }
    return id;
}

bool canvasAddCommandTo(Canvas& c, uint16_t menu, const char* name, UiCommandFn fn, void* user) {
    if (c.state == nullptr) {
        return false;
    }
    return uiCommandAddTo(*c.state, menu, name, fn, user);
}

uint16_t canvasPage(const Canvas& c, int page) {
    if (c.state == nullptr || page < 0 || page > 2) {
        return kUiNone;
    }
    return c.state->pages[page];
}

uint16_t canvasNode(Canvas& c, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    if (c.state == nullptr) {
        return kUiNone;
    }
    return uiNode(*c.state, parent, flex, paint);
}

void canvasText(Canvas& c, uint16_t id, const char* text) {
    if (c.state != nullptr) {
        uiText(*c.state, id, text);
    }
}

void canvasStamp(Canvas& c, uint16_t id, UiToken token) {
    if (c.state != nullptr) {
        uiStamp(*c.state, id, token);
    }
}

bool canvasSetFont(Canvas& c, const char* path, float pixelSize) {
    if (c.state == nullptr || c.device == nullptr || c.heaps == nullptr || c.prims.mapped == nullptr) {
        return false;
    }
    if (!fontLoadSdf(*c.state, *c.device, *c.heaps, c.font, path, pixelSize)) {
        return false;
    }
    auto* table = reinterpret_cast<float*>(static_cast<std::byte*>(c.prims.mapped) + kUiFontBase);
    for (uint32_t i = 0; i < kUiFontGlyphs; ++i) {
        table[i * 4 + 0] = c.state->glyphU0[i];
        table[i * 4 + 1] = c.state->glyphV0[i];
        table[i * 4 + 2] = c.state->glyphU1[i];
        table[i * 4 + 3] = c.state->glyphV1[i];
    }
    gpuBufferFlush(c.prims, *c.device, kUiFontBase, kUiFontBytes);
    c.state->look.glyphH = pixelSize;
    const int em = static_cast<int>('M' - 32);
    if (c.state->fontPx > 1.0f && c.state->glyphAdv[em] > 0.0f) {
        c.state->look.advance = c.state->glyphAdv[em] * (pixelSize / c.state->fontPx);
    }
    uiRestyle(*c.state);
    return true;
}

void canvasShadow(Canvas& c, uint16_t id, float shadow) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* paint = c.state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->shadow = shadow;
    c.state->visualDirty = true;
}

void canvasTextStyle(Canvas& c, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading) {
    if (c.state != nullptr) {
        uiTextStyle(*c.state, id, align, ellipsis, valign, wrap, deco, leading);
    }
}

uint16_t canvasTextId(const Canvas& c, uint16_t id) {
    if (c.state == nullptr) {
        return kUiNone;
    }
    return uiTextId(*c.state, id);
}

uint16_t canvasIcon(Canvas& c, uint16_t parent, uint8_t slot, float w, float h) {
    if (c.state == nullptr) {
        return kUiNone;
    }
    return uiIcon(*c.state, parent, slot, w, h);
}

void canvasBorder(Canvas& c, uint16_t id, float width, float r, float g, float b, float radius) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* paint = c.state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->borderW = width;
    paint->br = r;
    paint->bg = g;
    paint->bb = b;
    if (radius >= 0.0f) {
        paint->radius = radius;
    }
    c.state->visualDirty = true;
}

void canvasPad(Canvas& c, uint16_t id, float left, float right, float top, float bottom) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* flex = c.state->ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->pad = 0.0f;
    flex->padL = left;
    flex->padR = right;
    flex->padT = top;
    flex->padB = bottom;
    c.state->layoutDirty = true;
}

void canvasHover(Canvas& c, uint16_t id, float r, float g, float b) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* paint = c.state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->hr = r;
    paint->hg = g;
    paint->hb = b;
    c.state->visualDirty = true;
}

void canvasDisable(Canvas& c, uint16_t id, bool disabled) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* paint = c.state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->disabled = disabled ? 1 : 0;
    c.state->visualDirty = true;
}

void canvasField(Canvas& c, uint16_t id, const char* text, uint8_t filter, bool clear) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    UiField field{};
    uint8_t n = 0;
    if (text != nullptr) {
        while (text[n] != '\0' && n < 95) {
            field.bytes[n] = text[n];
            ++n;
        }
    }
    field.bytes[n] = '\0';
    field.len = n;
    field.caret = n;
    field.anchor = n;
    field.filter = filter;
    field.clear = clear ? 1 : 0;
    c.state->ent[id].set<UiField>(field);
    if (c.state->fieldMenu != kUiNone) {
        c.state->contextOf[id] = c.state->fieldMenu;
    }
    c.state->visualDirty = true;
}

void canvasFieldExtra(Canvas& c, uint16_t id, const char* placeholder, uint8_t maxChars, bool required, bool readOnly, bool password) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* field = c.state->ent[id].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    field->maxChars = maxChars;
    field->required = required ? 1 : 0;
    field->readOnly = readOnly ? 1 : 0;
    field->password = password ? 1 : 0;
    field->hintLen = 0;
    if (placeholder != nullptr) {
        while (placeholder[field->hintLen] != '\0' && field->hintLen < 39) {
            field->hint[field->hintLen] = placeholder[field->hintLen];
            ++field->hintLen;
        }
    }
    field->hint[field->hintLen] = '\0';
    c.state->visualDirty = true;
}

void canvasCheck(Canvas& c, uint16_t id, uint8_t kind, uint8_t group, bool on) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    c.state->ent[id].set<UiCheck>({on ? uint8_t{1} : uint8_t{0}, kind, group});
    auto* flex = c.state->ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex->width = 100.0f;
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        flex->height = 32.0f;
    }
    c.state->layoutDirty = true;
    c.state->visualDirty = true;
}

bool canvasTakeClip(Canvas& c, char* dst, uint8_t cap) {
    if (c.state == nullptr || c.state->clipPut == 0 || dst == nullptr || cap == 0) {
        return false;
    }
    const uint8_t n = c.state->clipLen < static_cast<uint8_t>(cap - 1) ? c.state->clipLen : static_cast<uint8_t>(cap - 1);
    for (uint8_t i = 0; i < n; ++i) {
        dst[i] = c.state->clip[i];
    }
    dst[n] = '\0';
    c.state->clipPut = 0;
    return n > 0;
}

void canvasSetPaint(Canvas& c, uint16_t id, float r, float g, float b, float a) {
    if (c.state == nullptr || id >= c.state->count) {
        return;
    }
    auto* paint = c.state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->r = r;
    paint->g = g;
    paint->b = b;
    paint->a = a;
    paint->token = static_cast<uint8_t>(UiToken::Custom);
    c.state->visualDirty = true;
}

void canvasAnimate(Canvas& c, uint16_t id, float r, float g, float b, float a) {
    if (c.state != nullptr) {
        uiMotionTo(*c.state, id, r, g, b, a);
        c.state->visualDirty = true;
    }
}

void canvasChrome(const Canvas& c, float& titleBar, float& resizeBorder) {
    titleBar = 40.0f;
    resizeBorder = 6.0f;
    if (c.state != nullptr) {
        titleBar = c.state->look.titleBar;
        resizeBorder = c.state->look.resizeBorder;
    }
}

UiEvent canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, float timeSec) {
    if (c.state == nullptr || c.state->count == 0 || w < 1.0f || h < 1.0f) {
        return UiEvent::None;
    }
    UiState& s = *c.state;
    s.time = timeSec;
    uiLayout(s, w, h);
    const UiEvent ev = uiApplyInput(s, input);
    if (s.layoutDirty) {
        uiLayout(s, w, h);
    }
    if (uiTickMotion(s)) {
        s.visualDirty = true;
    }
    if (s.visualDirty) {
        auto* bytes = static_cast<std::byte*>(c.prims.mapped);
        auto* dst = reinterpret_cast<UiPrimitive*>(bytes + kUiHeaderBytes);
        const uint32_t n = uiEmit(s, dst, kUiPrimCap);
        const UiGpuHeader hdr{w, h, n, 0};
        std::memcpy(bytes, &hdr, sizeof(hdr));
        c.drawCount = n;
        gpuBufferFlush(c.prims, d, 0, kUiHeaderBytes + sizeof(UiPrimitive) * n);
        s.visualDirty = false;
    }
    return ev;
}

void canvasTakeWindowOps(Canvas& c, WindowOps& out) {
    out = {};
    if (c.state == nullptr) {
        return;
    }
    UiState& s = *c.state;
    out.close = s.opClose;
    out.minimize = s.opMinimize;
    out.toggleMaximize = s.opMaximize;
    out.alwaysOnTop = s.opTop;
    out.opacity = s.opOpacity;
    out.openTool = s.opTool;
    out.openColor = s.opColor;
    out.openFile = s.opFile;
    s.opClose = false;
    s.opTool = false;
    s.opColor = false;
    s.opFile = false;
    s.opMinimize = false;
    s.opMaximize = false;
    s.opTop = -1;
    s.opOpacity = -1.0f;
}

bool canvasAddCommand(Canvas& c, const char* name, UiCommandFn fn, void* user) {
    if (c.state == nullptr) {
        return false;
    }
    return uiCommandAdd(*c.state, name, fn, user);
}

void canvasMenuStyle(Canvas& c, float width, float itemH, float pad, float gap) {
    if (c.state == nullptr || c.state->menu == kUiNone) {
        return;
    }
    UiState& s = *c.state;
    s.menuW = width;
    s.menuItemH = itemH;
    s.menuPad = pad;
    s.menuGap = gap;
    auto* flex = s.ent[s.menu].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->width = width;
        flex->pad = pad;
        flex->gap = gap;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.parentOf[id] != s.menu) {
            continue;
        }
        auto* item = s.ent[id].try_get_mut<UiFlex>();
        if (item != nullptr) {
            item->height = itemH;
        }
    }
    s.layoutDirty = true;
}

bool canvasWantsText(const Canvas& c) {
    if (c.state == nullptr) {
        return false;
    }
    if (c.state->plug.wantsText != nullptr && c.state->plug.wantsText(*c.state)) {
        return true;
    }
    if (c.state->focused == kUiNone) {
        return false;
    }
    return c.state->ent[c.state->focused].try_get<UiField>() != nullptr;
}

void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc) {
    rhiBufferBarrier(
        cmd,
        c.prims.buffer,
        0,
        c.prims.size,
        VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    rhiImageBarrier(
        cmd,
        sc.images[fc.imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkClearValue clear{};
    clear.color.float32[0] = 0.102f;
    clear.color.float32[1] = 0.106f;
    clear.color.float32[2] = 0.122f;
    clear.color.float32[3] = 1.0f;
    VkRenderingAttachmentInfo color{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = sc.views[fc.imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear,
    };
    VkRenderingInfo ri{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, sc.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vkCmdBeginRendering(cmd, &ri);
    if (c.drawCount > 0 && c.vs.handle != VK_NULL_HANDLE) {
        cmdBindVertFrag(cmd, c.vs.handle, c.fs.handle, c.nullMesh);
        cmdSetGraphicsDynamic(cmd, sc.extent, 1, false, true);
        vkCmdDraw(cmd, c.drawCount * 6, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);
}

} // namespace burnhope
