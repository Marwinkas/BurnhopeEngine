#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/UiPhoto.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace burnhope {
namespace {
void putFloat(uint32_t& dst, float v) {
    dst = std::bit_cast<uint32_t>(v);
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
        const UiScissor* scissor = s.ent[p].try_get<UiScissor>();
        const bool clip = scissor != nullptr && scissor->enabled
            || (flex != nullptr && (flex->overflow == UiOverflow::Hidden || flex->overflow == UiOverflow::Scroll));
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

uint32_t uiEmitSprite(
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

uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap) {
    uint16_t order[kUiCap];
    uint16_t nOrder = 0;
    uint16_t childHead[kUiCap];
    uint16_t childNext[kUiCap];
    uint16_t childTail[kUiCap];
    for (uint16_t i = 0; i < s.count && i < kUiCap; ++i) {
        childHead[i] = kUiNone;
        childNext[i] = kUiNone;
        childTail[i] = kUiNone;
    }
    uint16_t roots[kUiCap];
    uint16_t nRoots = 0;
    for (uint16_t id = 0; id < s.count && id < kUiCap; ++id) {
        if (!s.ent[id].is_alive()) {
            continue;
        }
        const uint16_t parent = s.parentOf[id];
        if (parent == kUiNone || parent >= s.count || !s.ent[parent].is_alive()) {
            if (nRoots < kUiCap) {
                roots[nRoots++] = id;
            }
            continue;
        }
        childNext[id] = kUiNone;
        if (childHead[parent] == kUiNone) {
            childHead[parent] = id;
        } else if (childTail[parent] < kUiCap) {
            childNext[childTail[parent]] = id;
        }
        childTail[parent] = id;
    }
    uint16_t stack[kUiCap];
    uint16_t sp = 0;
    for (uint16_t i = nRoots; i > 0; --i) {
        stack[sp++] = roots[i - 1];
    }
    while (sp > 0 && nOrder < kUiCap) {
        const uint16_t id = stack[--sp];
        order[nOrder++] = id;
        const uint16_t begin = sp;
        for (uint16_t child = childHead[id]; child != kUiNone && child < kUiCap && sp < kUiCap; child = childNext[child]) {
            stack[sp++] = child;
        }
        for (uint16_t a = begin, b = sp; a + 1 < b; ++a) {
            --b;
            const uint16_t swap = stack[a];
            stack[a] = stack[b];
            stack[b] = swap;
        }
    }

    const UiDrawHooks* hooks = s.world.try_get<UiDrawHooks>();
    uint32_t n = 0;
    for (int pass = 0; pass < 3; ++pass) {
        for (uint16_t k = 0; k < nOrder; ++k) {
            const uint16_t id = order[k];
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
            if (!emitShown(s, id)) {
                continue;
            }
            const UiPaint* paint = s.ent[id].try_get<UiPaint>();
            if (paint == nullptr) {
                continue;
            }
            UiBox ancestor{};
            const bool hasClip = clipAncestors(s, id, ancestor);
            const UiBox* clip = hasClip ? &ancestor : nullptr;
            if (hasClip && (ancestor.w < 0.5f || ancestor.h < 0.5f
                || s.box[id].x >= ancestor.x + ancestor.w || ancestor.x >= s.box[id].x + s.box[id].w
                || s.box[id].y >= ancestor.y + ancestor.h || ancestor.y >= s.box[id].y + s.box[id].h)) {
                continue;
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
            float r = paint->r;
            float g = paint->g;
            float b = paint->b;
            float a = paint->a;
            if (s.anim[id].on && s.anim[id].dur > 0.0f) {
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
            uiHoverTint(s, id, r, g, b, a);
            if (const UiImage* image = s.ent[id].try_get<UiImage>()) {
                n = uiEmitSprite(dst, n, cap, s.box[id], *image, clip, paint->blur, r, g, b);
            } else {
                n = uiEmitPanel(s, id, dst, n, cap, clip, r, g, b, a);
                if (!s.ent[id].has<UiTextInput>()) {
                    n = uiEmitTextNode(s, id, dst, n, cap, clip, r, g, b, a);
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
