#include "ui/PrimitiveBatch.hpp"
#include "ui/Bar.hpp"
#include "memory/FrameArena.hpp"
#include "platform/Check.hpp"
#include "ui/State.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace burnhope {
namespace {

struct UiBarScratch {
    FrameArena arena{};
};

FrameArena& barArena(UiState& s) {
    UiBarScratch& scratch = s.world.ensure<UiBarScratch>();
    if (scratch.arena.mem == nullptr) {
        // Провал оставляет арену пустой: frameArenaAlloc вернёт nullptr, и бар не добавится.
        const bool created = frameArenaCreate(scratch.arena, 64 * 1024);
        BH_ASSERT(created, "ui bar arena");
        static_cast<void>(created);
    }
    return scratch.arena;
}

} // namespace

void uiNodeBarPush(UiState& s, UiNodeBars& bars, const UiBar& bar) {
    UiBar* slot = static_cast<UiBar*>(frameArenaAlloc(barArena(s), sizeof(UiBar), alignof(UiBar)));
    if (slot == nullptr) {
        return;
    }
    *slot = bar;
    if (bars.bar == nullptr) {
        bars.bar = slot;
    }
    ++bars.n;
}

void uiBarScratchReset(UiState& s) {
    if (UiBarScratch* scratch = s.world.try_get_mut<UiBarScratch>()) {
        frameArenaReset(scratch->arena);
    }
}

void uiBarScratchShutdown(UiState& s) {
    if (UiBarScratch* scratch = s.world.try_get_mut<UiBarScratch>()) {
        frameArenaDestroy(scratch->arena);
    }
}

namespace {
void putFloat(uint32_t& dst, float v) {
    dst = std::bit_cast<uint32_t>(v);
}

bool emitShown(const UiState& s, uint16_t id) {
    return s.vis[id].hidden == 0 && s.box[id].w >= 1.0f && s.box[id].h >= 1.0f;
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
    float tintB,
    float tintA) {
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
    p.color[3] = tintA;
    p.texId = image.slot;
    p.flags = kUiFlagPhoto;
    putFloat(p.pad1, std::clamp(blur, 0.0f, 1.0f));
    p.extra[0] = image.viewX;
    p.extra[1] = base + image.viewY * span;
    p.extra[2] = image.viewX + image.viewW;
    p.extra[3] = base + (image.viewY + image.viewH) * span;
    if (clip != nullptr) {
        p.flags |= kUiFlagClip;
        p.corner[0] = clip->x;
        p.corner[1] = clip->y;
        p.corner[2] = clip->w;
        p.corner[3] = clip->h;
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
        map.corner[0] = clip->x;
        map.corner[1] = clip->y;
        map.corner[2] = clip->w;
        map.corner[3] = clip->h;
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
    if (id >= s.count || s.vis[id].clipped == 0) {
        return false;
    }
    out = s.vis[id].clip;
    return true;
}

void uiDrawAdd(UiState& s, UiDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.drawHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void uiDrawUnderAdd(UiState& s, UiDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.underHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void uiDrawDecorAdd(UiState& s, UiDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.decorHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void uiPlaceAdd(UiState& s, void (*fn)(UiState& s)) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.placeHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void uiOverlayAdd(UiState& s, UiOverlayDrawFn fn) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.overlayHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

static uint32_t emitBars(const UiState& s, UiPrimitive* dst, uint32_t n, uint32_t cap) {
    uint16_t ids[kUiCap];
    const uint16_t holders = uiCollect<UiBar>(s, ids, kUiCap);
    for (uint16_t k = 0; k < holders; ++k) {
        const uint16_t id = ids[k];
        const UiBar* bar = s.ent[id].try_get<UiBar>();
        if (bar == nullptr || bar->on == 0 || bar->w < 0.5f || bar->h < 0.5f) {
            continue;
        }
        UiBox clip = s.box[id];
        UiBox ancestor{};
        if (uiClipBox(s, id, ancestor) && ancestor.w > 0.5f && ancestor.h > 0.5f) {
            const float x0 = clip.x > ancestor.x ? clip.x : ancestor.x;
            const float y0 = clip.y > ancestor.y ? clip.y : ancestor.y;
            const float x1 = clip.x + clip.w < ancestor.x + ancestor.w ? clip.x + clip.w : ancestor.x + ancestor.w;
            const float y1 = clip.y + clip.h < ancestor.y + ancestor.h ? clip.y + clip.h : ancestor.y + ancestor.h;
            clip.x = x0;
            clip.y = y0;
            clip.w = x1 > x0 ? x1 - x0 : 0.0f;
            clip.h = y1 > y0 ? y1 - y0 : 0.0f;
        }
        n = emitRect(dst, n, cap, bar->x, bar->y, bar->w, bar->h, bar->r, bar->g, bar->b, bar->a, bar->radius, &clip);
    }
    return n;
}

uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap) {
    uiBarScratchReset(s);
    for (void (*fn)(UiState&) : s.placeHook) {
        if (fn != nullptr) {
            fn(s);
        }
    }
    uiCacheVisibility(s);
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
    auto link = [&](uint16_t id) {
        if (!s.ent[id].is_alive()) {
            return;
        }
        const uint16_t parent = s.parentOf[id];
        if (parent == kUiNone || parent >= s.count || !s.ent[parent].is_alive()) {
            if (nRoots < kUiCap) {
                roots[nRoots++] = id;
            }
            return;
        }
        childNext[id] = kUiNone;
        if (childHead[parent] == kUiNone) {
            childHead[parent] = id;
        } else if (childTail[parent] < kUiCap) {
            childNext[childTail[parent]] = id;
        }
        childTail[parent] = id;
    };
    for (uint16_t id = 0; id < s.count && id < kUiCap; ++id) {
        if (!s.ent[id].has<UiPaintLast>()) {
            link(id);
        }
    }
    for (uint16_t id = 0; id < s.count && id < kUiCap; ++id) {
        if (s.ent[id].has<UiPaintLast>()) {
            link(id);
        }
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

    uint32_t n = 0;
    for (int pass = 0; pass < 3; ++pass) {
        for (uint16_t k = 0; k < nOrder; ++k) {
            const uint16_t id = order[k];
            const bool popup = s.vis[id].popup != 0;
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
            const bool hasClip = uiClipBox(s, id, ancestor);
            const UiBox* clip = hasClip ? &ancestor : nullptr;
            if (hasClip && (ancestor.w < 0.5f || ancestor.h < 0.5f
                || s.box[id].x >= ancestor.x + ancestor.w || ancestor.x >= s.box[id].x + s.box[id].w
                || s.box[id].y >= ancestor.y + ancestor.h || ancestor.y >= s.box[id].y + s.box[id].h)) {
                continue;
            }
            if (s.plug.draw != nullptr && s.plug.draw(s, id, dst, n, cap)) {
                continue;
            }
            bool took = false;
            for (std::size_t hi = 0; hi < s.drawHook.size(); ++hi) {
                if (s.drawHook[hi] != nullptr && s.drawHook[hi](s, id, dst, n, cap)) {
                    took = true;
                    break;
                }
            }
            if (took) {
                continue;
            }
            float r = paint->r;
            float g = paint->g;
            float b = paint->b;
            float a = paint->a;
            a *= s.vis[id].fadeA;
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
            float glyphA = 1.0f;
            const uint16_t host = s.parentOf[id];
            if (paint->plate != 0) {
                const bool hot = s.hovered == id || (host < s.count && s.hovered == host);
                const float* ink = paint->picked != 0 ? kUiToolActive : (hot ? kUiToolHover : kUiToolIdle);
                r = ink[0];
                g = ink[1];
                b = ink[2];
                a = 0.0f;
            }
            const UiImage* image = s.ent[id].try_get<UiImage>();
            if (image != nullptr && image->inset <= 0.0f) {
                n = uiEmitSprite(dst, n, cap, s.box[id], *image, clip, paint->blur, r, g, b, glyphA);
            } else {
                n = uiEmitPanel(s, id, dst, n, cap, clip, r, g, b, a, glyphA);
                for (std::size_t ui = 0; ui < s.underHook.size(); ++ui) {
                    if (s.underHook[ui] != nullptr) {
                        s.underHook[ui](s, id, dst, n, cap);
                    }
                }
                const UiText* shown = s.ent[id].try_get<UiText>();
                if (shown == nullptr || shown->ownRun == 0) {
                    n = uiEmitTextNode(s, id, dst, n, cap, clip, r, g, b, a);
                }
                if (image != nullptr) {
                    // Ghost icon buttons (plate) tint the sprite with their paint colour; solid buttons show it untinted.
                    const UiBox& host = s.box[id];
                    const UiBox spot{host.x + (host.w - image->inset) * 0.5f, host.y + (host.h - image->inset) * 0.5f, image->inset, image->inset};
                    const float* ink = paint->plate != 0 ? nullptr : kUiWhite;
                    n = uiEmitSprite(
                        dst, n, cap, spot, *image, clip, paint->blur,
                        ink != nullptr ? ink[0] : r, ink != nullptr ? ink[1] : g, ink != nullptr ? ink[2] : b, glyphA);
                }
            }
            if (const UiNodeBars* bars = s.ent[id].try_get<UiNodeBars>(); bars != nullptr && bars->bar != nullptr) {
                for (uint16_t bi = 0; bi < bars->n; ++bi) {
                    const UiBar& bar = bars->bar[bi];
                    if (bar.on == 0 || bar.w < 0.5f || bar.h < 0.5f) {
                        continue;
                    }
                    if ((bar.flags & kUiFlagStroke) != 0) {
                        n = emitMark(dst, n, cap, bar.x, bar.y, bar.w, bar.h, bar.r, bar.g, bar.b, bar.a, bar.radius, bar.spread, kUiFlagStroke, clip);
                    } else {
                        n = emitRect(dst, n, cap, bar.x, bar.y, bar.w, bar.h, bar.r, bar.g, bar.b, bar.a, bar.radius, clip);
                    }
                }
                if (bars->icon != 0 && n < cap) {
                    constexpr float kSide = 12.0f;
                    const float side = bars->iconS > 1.0f ? bars->iconS : kSide;
                    UiPrimitive& icon = dst[n++];
                    icon = {};
                    icon.posX = bars->iconX;
                    icon.posY = bars->iconY;
                    icon.sizeX = side;
                    icon.sizeY = side;
                    icon.color[0] = bars->iconR;
                    icon.color[1] = bars->iconG;
                    icon.color[2] = bars->iconB;
                    icon.color[3] = bars->iconA;
                    icon.texId = bars->icon;
                    icon.flags = kUiFlagIcon;
                }
            }
            if (const UiTextInk* ink = s.ent[id].try_get<UiTextInk>(); ink != nullptr && ink->on != 0) {
                const UiText* inkText = s.ent[id].try_get<UiText>();
                if (inkText != nullptr && inkText->len > 0) {
                    const UiBox slot{ink->x, ink->y, ink->w, ink->h};
                    UiBox limit{ink->cx, ink->cy, ink->cw, ink->ch};
                    const TextRun run = textRun(s, *inkText, slot);
                    if (run.lines > 0) {
                        n = uiEmitRun(s, dst, n, cap, run, ink->r, ink->g, ink->b, ink->a, ink->clip != 0 ? &limit : nullptr);
                    }
                }
            }
            for (std::size_t di = 0; di < s.decorHook.size(); ++di) {
                if (s.decorHook[di] != nullptr) {
                    s.decorHook[di](s, id, dst, n, cap);
                }
            }
        }
    }
    n = emitBars(s, dst, n, cap);
    if (const UiStamps* stamps = s.world.try_get<UiStamps>()) {
        for (uint16_t i = 0; i < stamps->n; ++i) {
            if (stamps->bar == nullptr) {
                break;
            }
            const UiBar& bar = stamps->bar[i];
            if (bar.on == 0 || bar.w < 0.5f || bar.h < 0.5f) {
                continue;
            }
            n = emitRect(dst, n, cap, bar.x, bar.y, bar.w, bar.h, bar.r, bar.g, bar.b, bar.a, bar.radius, nullptr);
        }
    }
    for (std::size_t i = 0; i < s.overlayHook.size(); ++i) {
        if (s.overlayHook[i] != nullptr) {
            s.overlayHook[i](s, dst, n, cap);
        }
    }
    if (s.plug.over != nullptr) {
        s.plug.over(s, dst, n, cap);
    }
    return n;
}
} // namespace burnhope
