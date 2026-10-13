#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace burnhope {
namespace {
void putFloat(uint32_t& dst, float v) {
    dst = std::bit_cast<uint32_t>(v);
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
    const float scale = ui.font.ready && ui.font.fontPx > 1.0f ? ui.look.glyphH / ui.font.fontPx : 1.0f;
    const float em = ui.look.glyphH > 1.0f ? ui.look.glyphH : 18.0f;
    float fit = emScale > 0.05f ? emScale : 1.0f;
    if (emScale <= 0.05f && em > box.h && em > 0.5f) {
        fit = box.h / em;
    }
    const float used = scale * fit;
    const float baseline = box.y + (box.h - em * fit) * 0.5f + em * fit * 0.76f;
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
        const UiGlyphMetric& glyph = ui.font.glyphs[gi];
        const float adv = ui.font.ready ? glyph.advance * used : ui.look.advance;
        float gw = ui.font.ready ? glyph.w * used : ui.look.glyphW;
        float gh = ui.font.ready ? glyph.h * used : ui.look.glyphH;
        float x = pen + (ui.font.ready ? glyph.bx * used : 0.0f);
        float y = ui.font.ready ? baseline - glyph.by * used : box.y + (box.h - gh) * 0.5f;
        if (len == 1 && gw > 0.5f && gh > 0.5f) {
            y = box.y + (box.h - gh) * 0.5f;
            if (center) {
                x = box.x + (box.w - gw) * 0.5f;
            }
        }
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

bool emitShown(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.tight[p] != 0) {
            return false;
        }
        if (s.culled[p] != 0 || !nodeShown(s, p)) {
            return false;
        }
    }
    return s.box[id].w >= 1.0f && s.box[id].h >= 1.0f;
}

bool clipAncestors(const UiState& s, uint16_t id, UiBox& out) {
    bool any = false;
    for (uint16_t p = s.parentOf[id]; p != kUiNone; p = s.parentOf[p]) {
        const UiPaint* paint = s.ent[p].try_get<UiPaint>();
        const UiFlex* flex = s.ent[p].try_get<UiFlex>();
        const bool clip = (paint != nullptr && (paint->role == static_cast<uint8_t>(UiRole::Scroll)
            || paint->role == static_cast<uint8_t>(UiRole::Panel)))
            || (flex != nullptr && (flex->overflow == 1 || flex->overflow == 2));
        if (!clip) {
            continue;
        }
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

void boost(float& r, float& g, float& b) {
    r = r + 0.08f > 1.0f ? 1.0f : r + 0.08f;
    g = g + 0.08f > 1.0f ? 1.0f : g + 0.08f;
    b = b + 0.08f > 1.0f ? 1.0f : b + 0.08f;
}
} // namespace

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
    p.corner[0] = radius;
    p.corner[1] = radius;
    p.corner[2] = radius;
    p.corner[3] = radius;
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
    p.corner[0] = radius;
    p.corner[1] = radius;
    p.corner[2] = radius;
    p.corner[3] = radius;
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

uint32_t emitPhoto(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox& box,
    const UiImage& image,
    const UiBox* clip,
    float blur,
    float tintR,
    float tintG,
    float tintB) {
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
    p.color[0] = tintR;
    p.color[1] = tintG;
    p.color[2] = tintB;
    p.color[3] = 1.0f;
    p.texId = image.slot;
    p.flags = kUiFlagPhoto;
    putFloat(p.pad1, std::clamp(blur, 0.0f, 1.0f));
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


bool uiClipBox(const UiState& s, uint16_t id, UiBox& out) {
    return clipAncestors(s, id, out);
}

void uiDrawAdd(UiState& s, UiDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    UiDrawHooks& hooks = s.world.ensure<UiDrawHooks>();
    for (uint8_t i = 0; i < hooks.n; ++i) {
        if (hooks.fn[i] == fn) {
            return;
        }
    }
    if (hooks.n < 4) {
        hooks.fn[hooks.n++] = fn;
    }
}

void uiDrawDecorAdd(UiState& s, UiDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    UiDrawHooks& hooks = s.world.ensure<UiDrawHooks>();
    for (uint8_t i = 0; i < hooks.decorN; ++i) {
        if (hooks.decor[i] == fn) {
            return;
        }
    }
    if (hooks.decorN < 8) {
        hooks.decor[hooks.decorN++] = fn;
    }
}

void uiOverlayAdd(UiState& s, UiOverlayDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    UiOverlayHooks& hooks = s.world.ensure<UiOverlayHooks>();
    for (uint8_t i = 0; i < hooks.n; ++i) {
        if (hooks.fn[i] == fn) {
            return;
        }
    }
    if (hooks.n < 4) {
        hooks.fn[hooks.n++] = fn;
    }
}

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

uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap) {
    uint16_t ids[kUiCap];
    uint16_t nIds = 0;
    s.paints.each([&](flecs::entity, UiSlot& slot, UiPaint&) {
        if (nIds < kUiCap) {
            ids[nIds++] = slot.index;
        }
    });
    std::sort(ids, ids + nIds);

    const UiDrawHooks* hooks = s.world.try_get<UiDrawHooks>();
    uint32_t n = 0;
    for (int pass = 0; pass < 3; ++pass) {
    for (uint16_t k = 0; k < nIds; ++k) {
        const uint16_t id = ids[k];
        bool popup = false;
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].has<UiPopup>()) {
                popup = true;
                break;
            }
        }
        if (pass == 2) {
            if (!popup) {
                continue;
            }
        } else if (popup || uiIsOverlay(s, id) != (pass == 1)) {
            continue;
        }
        const UiPaint* paintPtr = s.ent[id].try_get<UiPaint>();
        if (paintPtr == nullptr || id >= s.count) {
            continue;
        }
        const UiPaint& paint = *paintPtr;
        const auto role = static_cast<UiRole>(paint.role);
        if (role == UiRole::Hidden || !emitShown(s, id)) {
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
        if (hooks != nullptr) {
            bool took = false;
            for (uint8_t hi = 0; hi < hooks->n; ++hi) {
                if (hooks->fn[hi] != nullptr && hooks->fn[hi](s, id, dst, n, cap)) {
                    took = true;
                    break;
                }
            }
            if (took) {
                continue;
            }
        }
        if (const UiImage* image = s.ent[id].try_get<UiImage>()) {
            float pr = r;
            float pg = g;
            float pb = b;
            if (role == UiRole::Button && pr * 0.30f + pg * 0.59f + pb * 0.11f < 0.55f) {
                pr = 1.0f;
                pg = 1.0f;
                pb = 1.0f;
            }
            n = emitPhoto(dst, n, cap, box, *image, clip, paint.blur, pr, pg, pb);
            continue;
        }
        if (role == UiRole::Label && r + g + b < 0.25f) {
            r = s.look.text[0];
            g = s.look.text[1];
            b = s.look.text[2];
            if (r + g + b < 0.25f) {
                r = 0.92f;
                g = 0.93f;
                b = 0.95f;
            }
        }
        if (role == UiRole::Label) {
            const uint16_t parentId = s.parentOf[id];
            if (parentId < s.count) {
                const UiPaint* parentPaint = s.ent[parentId].try_get<UiPaint>();
                if (parentPaint != nullptr) {
                    const auto parentRole = static_cast<UiRole>(parentPaint->role);
                    if (parentPaint->disabled != 0) {
                        a *= 0.45f;
                    }
                    if ((parentRole == UiRole::Button || parentRole == UiRole::Close || parentRole == UiRole::Check)
                        && parentPaint->a > 0.2f) {
                        const bool parentHot = parentPaint->disabled == 0 && parentId == s.hovered;
                        textInk(parentPaint->r, parentPaint->g, parentPaint->b, parentHot, r, g, b);
                    }
                }
            }
            const UiText* text = s.ent[id].try_get<UiText>();
            if (text != nullptr) {
                UiBox limit = box;
                if (parentId != kUiNone && s.box[parentId].w > 1.0f && s.box[parentId].h > 1.0f) {
                    const UiBox& parent = s.box[parentId];
                    const float x0 = limit.x > parent.x ? limit.x : parent.x;
                    const float y0 = limit.y > parent.y ? limit.y : parent.y;
                    const float x1 = limit.x + limit.w < parent.x + parent.w ? limit.x + limit.w : parent.x + parent.w;
                    const float y1 = limit.y + limit.h < parent.y + parent.h ? limit.y + limit.h : parent.y + parent.h;
                    if (x1 - x0 > 1.0f && y1 - y0 > 1.0f) {
                        limit.x = x0;
                        limit.y = y0;
                        limit.w = x1 - x0;
                        limit.h = y1 - y0;
                    }
                }
                if (hasClip && ancestor.w > 1.0f && ancestor.h > 1.0f) {
                    const float x0 = limit.x > ancestor.x ? limit.x : ancestor.x;
                    const float y0 = limit.y > ancestor.y ? limit.y : ancestor.y;
                    const float x1 = limit.x + limit.w < ancestor.x + ancestor.w ? limit.x + limit.w : ancestor.x + ancestor.w;
                    const float y1 = limit.y + limit.h < ancestor.y + ancestor.h ? limit.y + limit.h : ancestor.y + ancestor.h;
                    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) {
                        continue;
                    }
                    limit.x = x0;
                    limit.y = y0;
                    limit.w = x1 - x0;
                    limit.h = y1 - y0;
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
            const bool ghost = a < 0.05f;
            const bool hoverStroke = hot && role == UiRole::Button && ghost && paint.borderW > 0.4f;
            if (hot && role == UiRole::Button && ghost && !hoverStroke) {
                br = s.look.accent[0][0];
                bg = s.look.accent[0][1];
                bb = s.look.accent[0][2];
                ba = 0.35f;
            }
            const bool focusRing = paint.disabled == 0 && id == s.focused
                && (role == UiRole::Field || role == UiRole::Check || role == UiRole::Button);
            const float ring = hoverStroke ? paint.borderW
                : (paint.borderW > 0.4f && !ghost ? paint.borderW : (focusRing ? 1.5f : 0.0f));
            if (paint.shadow > 0.5f && ba > 0.02f) {
                const float sh = paint.shadow;
                n = emitMark(
                    dst, n, cap, box.x - sh, box.y - sh * 0.15f, box.w + sh * 2.0f, box.h + sh * 2.0f,
                    0.0f, 0.0f, 0.0f, 0.42f, paint.radius + sh, sh, kUiFlagShadow, clip);
            }
            const uint32_t beforeFill = n;
            if (role == UiRole::Panel && paint.ramp != 0) {
                n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, br, bg, bb, paint.ramp, kUiFlagRamp);
            } else {
                n = emitRect(dst, n, cap, box.x, box.y, box.w, box.h, br, bg, bb, ba, paint.radius, clip);
            }
            if (n > beforeFill && (paint.radiusTr >= 0.0f || paint.radiusBr >= 0.0f || paint.radiusBl >= 0.0f)) {
                UiPrimitive& filled = dst[n - 1];
                const float tl = paint.radius;
                filled.corner[0] = tl;
                filled.corner[1] = paint.radiusTr >= 0.0f ? paint.radiusTr : tl;
                filled.corner[2] = paint.radiusBr >= 0.0f ? paint.radiusBr : tl;
                filled.corner[3] = paint.radiusBl >= 0.0f ? paint.radiusBl : tl;
            }
            const bool hasTransform = paint.angle != 0.0f || paint.scaleX != 1.0f || paint.scaleY != 1.0f
                || paint.bright != 1.0f || paint.contrast != 1.0f;
            if (n > beforeFill && hasTransform && clip == nullptr) {
                // pad0/pad1 double as clip-rect storage above; transform/filter needs both,
                // so a clipped panel cannot also rotate/scale/filter in this one draw.
                UiPrimitive& filled = dst[n - 1];
                filled.flags |= kUiFlagTransform;
                uiPackTransform(filled.pad0, filled.pad1, paint.angle, paint.scaleX, paint.scaleY, paint.bright, paint.contrast);
            }
            if ((role == UiRole::Button || role == UiRole::Check) && paint.shadow > 0.5f && box.h > 10.0f && id != s.capture) {
                const float hr = br + 0.10f > 1.0f ? 1.0f : br + 0.10f;
                const float hg = bg + 0.10f > 1.0f ? 1.0f : bg + 0.10f;
                const float hb = bb + 0.08f > 1.0f ? 1.0f : bb + 0.08f;
                n = emitRect(dst, n, cap, box.x + 3.0f, box.y + 2.0f, box.w - 6.0f, 2.0f, hr, hg, hb, ba, 0.0f, clip);
            }
            if (ring > 0.4f) {
                const float orr = focusRing && paint.borderW < 0.4f ? s.look.accent[0][0] : paint.br;
                const float org = focusRing && paint.borderW < 0.4f ? s.look.accent[0][1] : paint.bg;
                const float orb = focusRing && paint.borderW < 0.4f ? s.look.accent[0][2] : paint.bb;
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
                (void)id;
            }
        }
        if ((role == UiRole::Button || role == UiRole::Close) && paint.icon != 0 && n < cap) {
            const UiText* host = s.ent[id].try_get<UiText>();
            const bool hasText = host != nullptr && host->len > 0;
            float ir = s.look.text[0];
            float ig = s.look.text[1];
            float ib = s.look.text[2];
            const float lum = ir * 0.30f + ig * 0.59f + ib * 0.11f;
            if (lum < 0.55f) {
                ir = 0.88f;
                ig = 0.88f;
                ib = 0.88f;
            }
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
            icon.posX = hasText ? box.x + 6.0f : box.x + (box.w - side) * 0.5f;
            icon.posY = box.y + (box.h - side) * 0.5f;
            icon.sizeX = side;
            icon.sizeY = side;
            icon.color[0] = ir;
            icon.color[1] = ig;
            icon.color[2] = ib;
            icon.color[3] = 1.0f;
            icon.texId = paint.icon;
            icon.flags = kUiFlagIcon;
            if (clip != nullptr) {
                icon.flags |= kUiFlagClip;
                icon.extra[1] = clip->x;
                icon.extra[2] = clip->y;
                icon.extra[3] = clip->w;
                putFloat(icon.pad0, clip->h);
            }
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
                n = emitRect(dst, n, cap, box.x, box.y + box.h * 0.38f, box.w, box.h * 0.24f, s.look.slider[0], s.look.slider[1], s.look.slider[2], 1.0f, 2.0f, clip);
                n = emitRect(dst, n, cap, box.x, box.y + box.h * 0.38f, fillW, box.h * 0.24f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, 2.0f, clip);
            }
            n = emitRect(dst, n, cap, box.x + fillW - 5.0f, box.y + 5.0f, 12.0f, box.h - 6.0f, 0.0f, 0.0f, 0.0f, 0.55f, 2.0f, clip);
            n = emitRect(dst, n, cap, box.x + fillW - 6.0f, box.y + 3.0f, 12.0f, box.h - 6.0f, 0.97f, 0.96f, 0.94f, 1.0f, 2.0f, clip);
        } else if (role == UiRole::Check) {
            const UiCheck* check = s.ent[id].try_get<UiCheck>();
            const bool radio = check != nullptr && check->kind == static_cast<uint8_t>(UiCheckKind::Radio);

            const float side = 18.0f;
            const float by = box.y + (box.h - side) * 0.5f;
            const float rad = radio ? side * 0.5f : 4.0f;
            n = emitRect(
                dst, n, cap, box.x + 8.0f, by, side, side, s.look.content[0], s.look.content[1], s.look.content[2], 1.0f, rad, clip);
            if (check != nullptr && check->on != 0) {
                if (radio) {
                    n = emitRect(dst, n, cap, box.x + 13.0f, by + 5.0f, side - 10.0f, side - 10.0f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, (side - 10.0f) * 0.5f, clip);
                } else {
                    n = emitRect(dst, n, cap, box.x + 12.0f, by + 4.0f, side - 8.0f, side - 8.0f, s.look.accent[1][0], s.look.accent[1][1], s.look.accent[1][2], 1.0f, 2.0f, clip);
                }
            }
        }
        if (role == UiRole::Button || role == UiRole::Close || role == UiRole::Check) {
            const UiText* host = s.ent[id].try_get<UiText>();
            if (host != nullptr && host->len > 0) {
                UiBox slot = box;
                if (role == UiRole::Check) {
                    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
                    const float inset = flex != nullptr && flex->padL > 1.0f ? flex->padL : 32.0f;
                    slot.x += inset;
                    slot.w = slot.w > inset + 4.0f ? slot.w - inset : 4.0f;
                }
                float tr = s.look.text[0];
                float tg = s.look.text[1];
                float tb = s.look.text[2];
                if (a > 0.2f) {
                    textInk(r, g, b, hot, tr, tg, tb);
                }
                const TextRun run = textRun(s, *host, slot);
                if (run.lines > 0) {
                    const float ta = paint.disabled != 0 ? a * 0.45f : a;
                    n = uiEmitRun(s, dst, n, cap, run, tr, tg, tb, ta > 0.05f ? ta : 1.0f, clip);
                }
            }
        }
        if (hooks != nullptr) {
            for (uint8_t di = 0; di < hooks->decorN; ++di) {
                if (hooks->decor[di] != nullptr) {
                    hooks->decor[di](s, id, dst, n, cap);
                }
            }
        }
    }
    }
    if (const UiOverlayHooks* overlays = s.world.try_get<UiOverlayHooks>()) {
        for (uint8_t i = 0; i < overlays->n; ++i) {
            if (overlays->fn[i] != nullptr) {
                overlays->fn[i](s, dst, n, cap);
            }
        }
    }
    if (s.plug.over != nullptr) {
        s.plug.over(s, dst, n, cap);
    }
    return n;
}
} // namespace burnhope
