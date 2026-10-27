#include "gui/render/GuiEmitter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string_view>

#include "gui/interaction/InteractTextEdit.hpp"
#include "gui/text/GuiTextEngine.hpp"

namespace burnhope::gui {
namespace {

struct EmitRootQuery {
    flecs::query<> roots;
};

struct Sink {
    flecs::world* world = nullptr;
    const GuiStyleSheet* sheet = nullptr;
    const GuiInteractState* interact = nullptr;
    UiPrimitive* out = nullptr;
    uint32_t cap = 0;
    uint32_t n = 0;
    float text[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float select[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

const EmitRootQuery& rootQuery(flecs::world& world) {
    if (const EmitRootQuery* cached = world.try_get<EmitRootQuery>()) {
        return *cached;
    }
    world.set<EmitRootQuery>({world.query_builder<>().with<UiRoot>().build()});
    return *world.try_get<EmitRootQuery>();
}

bool clipEmpty(const UiClip& c) {
    return c.w <= 0.0f || c.h <= 0.0f;
}

void putClip(UiPrimitive& p, const UiClip& c, bool inCorner) {
    p.flags |= kUiFlagClip;
    if (inCorner) {
        p.corner[0] = c.x;
        p.corner[1] = c.y;
        p.corner[2] = c.w;
        p.corner[3] = c.h;
        return;
    }
    p.extra[1] = c.x;
    p.extra[2] = c.y;
    p.extra[3] = c.w;
    p.pad0 = std::bit_cast<uint32_t>(c.h);
}

UiPrimitive* take(Sink& s) {
    if (s.n >= s.cap) {
        return nullptr;
    }
    UiPrimitive& p = s.out[s.n++];
    p = {};
    return &p;
}

void emitRect(Sink& s, float x, float y, float w, float h, const float rgba[4], float radius, const UiClip& clip) {
    if (w < 0.5f || h < 0.5f || rgba[3] <= 0.0f) {
        return;
    }
    UiPrimitive* p = take(s);
    if (p == nullptr) {
        return;
    }
    p->posX = x;
    p->posY = y;
    p->sizeX = w;
    p->sizeY = h;
    std::copy_n(rgba, 4, p->color);
    p->extra[0] = radius;
    std::fill_n(p->corner, 4, radius);
    putClip(*p, clip, false);
}

void emitStroke(Sink& s, const UiBox& b, const float rgba[4], float radius, float width, const UiClip& clip) {
    if (b.w < 0.5f || b.h < 0.5f || rgba[3] <= 0.0f) {
        return;
    }
    UiPrimitive* p = take(s);
    if (p == nullptr) {
        return;
    }
    p->posX = b.x;
    p->posY = b.y;
    p->sizeX = b.w;
    p->sizeY = b.h;
    std::copy_n(rgba, 4, p->color);
    p->extra[0] = radius;
    std::fill_n(p->corner, 4, radius);
    p->flags = kUiFlagStroke;
    p->pad1 = std::bit_cast<uint32_t>(width);
    putClip(*p, clip, false);
}

void emitPhoto(Sink& s, const UiBox& b, uint8_t slot, const float tint[4], const UiClip& clip) {
    if (b.w < 1.0f || b.h < 1.0f) {
        return;
    }
    UiPrimitive* p = take(s);
    if (p == nullptr) {
        return;
    }
    p->posX = b.x;
    p->posY = b.y;
    p->sizeX = b.w;
    p->sizeY = b.h;
    std::copy_n(tint, 4, p->color);
    p->texId = slot;
    p->flags = kUiFlagPhoto;
    p->extra[2] = 1.0f;
    p->extra[3] = 1.0f;
    putClip(*p, clip, true);
}

void paletteColor(const GuiStyleSheet& sheet, uint8_t token, const float tint[4], float rgba[4]) {
    guiStyleColor(sheet, token, rgba);
    for (int i = 0; i < 4; ++i) {
        rgba[i] *= tint[i];
    }
}

// Pixel rect of the glyph run that is visible inside the clip; used to cull glyphs.
bool glyphVisible(float x, float y, float w, float h, const UiClip& c) {
    return x < c.x + c.w && x + w > c.x && y < c.y + c.h && y + h > c.y;
}

struct TextArea {
    float x, y, w, h;
};

TextArea textArea(flecs::entity e, const UiBox& b) {
    TextArea a{b.x, b.y, b.w, b.h};
    const UiFlex* flex = e.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return a;
    }
    const float l = YGNodeLayoutGetPadding(flex->node, YGEdgeLeft);
    const float t = YGNodeLayoutGetPadding(flex->node, YGEdgeTop);
    const float r = YGNodeLayoutGetPadding(flex->node, YGEdgeRight);
    const float bt = YGNodeLayoutGetPadding(flex->node, YGEdgeBottom);
    a.x += l;
    a.y += t;
    a.w = std::max(0.0f, b.w - l - r);
    a.h = std::max(0.0f, b.h - t - bt);
    return a;
}

void emitText(Sink& s, flecs::entity e, const UiBox& box, const UiText& t, const UiClip& clip) {
    const GuiTextMetrics& m = s.sheet->text;
    const UiEditable* edit = e.try_get<UiEditable>();
    const bool focused = edit != nullptr && s.interact != nullptr && e.has<UiFocused>();
    if (t.len == 0 && !focused) {
        return;
    }
    const std::string_view str(t.text, t.len);
    const float px = static_cast<float>(t.size);
    const TextArea area = textArea(e, box);
    const float lineH = guiTextLineHeight(m, px);
    const bool wrap = edit == nullptr && area.h >= lineH * 1.5f;
    GuiTextLine lines[kGuiEmitLineCap];
    uint32_t count = guiTextWrap(m, str, px, wrap ? area.w : 0.0f, lines, kGuiEmitLineCap);
    count = std::min(count, kGuiEmitLineCap);
    if (count == 0) {
        lines[0] = {};
        count = 1;
    }
    const float block = lineH * static_cast<float>(count);
    const float top = count == 1 ? area.y + (area.h - block) * 0.5f : area.y;
    const float originX = edit != nullptr ? box.x + kGuiEditPadX - edit->scrollX : area.x;

    if (edit != nullptr && edit->caret != edit->selAnchor) {
        const uint32_t lo = std::min<uint32_t>(edit->caret, edit->selAnchor);
        const uint32_t hi = std::min<uint32_t>(std::max<uint32_t>(edit->caret, edit->selAnchor), t.len);
        const float x0 = originX + guiTextXFromCaret(m, str, px, lo);
        const float x1 = originX + guiTextXFromCaret(m, str, px, hi);
        emitRect(s, x0, top, x1 - x0, lineH, s.select, 0.0f, clip);
    }

    GuiGlyph unknown{};
    const int fallback = guiTextSlot(static_cast<uint32_t>('?'));
    for (uint32_t li = 0; li < count && s.n < s.cap; ++li) {
        const GuiTextLine& line = lines[li];
        const float lineTop = top + lineH * static_cast<float>(li);
        if (lineTop + lineH <= clip.y || lineTop >= clip.y + clip.h) {
            continue;
        }
        float pen = originX;
        if (edit == nullptr && t.align == 1) {
            pen = area.x + (area.w - line.width) * 0.5f;
        } else if (edit == nullptr && t.align == 2) {
            pen = area.x + area.w - line.width;
        }
        const float baseline = lineTop + m.ascent * px;
        const std::string_view run = str.substr(line.begin, line.end - line.begin);
        for (uint32_t at = 0; at < run.size() && s.n < s.cap;) {
            uint32_t step = 0;
            const uint32_t cp = guiTextDecode(run, at, step);
            if (step == 0) {
                break;
            }
            at += step;
            const bool known = guiTextGlyph(m, cp, unknown);
            const float x = pen + unknown.bearingX * px;
            const float y = baseline - unknown.bearingY * px;
            const float gw = unknown.w * px;
            const float gh = unknown.h * px;
            pen += unknown.advance * px;
            if (gw < 0.5f || gh < 0.5f || !glyphVisible(x, y, gw, gh, clip)) {
                continue;
            }
            const int slot = known ? guiTextSlot(cp) : fallback;
            UiPrimitive* p = take(s);
            if (p == nullptr) {
                break;
            }
            p->posX = x;
            p->posY = y;
            p->sizeX = gw;
            p->sizeY = gh;
            std::copy_n(s.text, 4, p->color);
            p->texId = static_cast<uint32_t>(slot < 0 ? 0 : slot);
            p->flags = kUiFlagGlyph;
            putClip(*p, clip, false);
        }
    }

    if (focused && (s.interact->caretTick % 60u) < 30u) {
        const uint32_t caret = std::min<uint32_t>(edit->caret, t.len);
        const float cx = originX + guiTextXFromCaret(m, str, px, caret);
        emitRect(s, cx, top, 1.5f, lineH, s.text, 0.0f, clip);
    }
}

void emitEntity(Sink& s, flecs::entity e, uint32_t depth);

void emitChildren(Sink& s, flecs::entity e, uint32_t depth) {
    if (depth + 1 >= kGuiEmitDepthCap) {
        return;
    }
    if (e.has(flecs::OrderedChildren)) {
        const ecs_entities_t kids = ecs_get_ordered_children(*s.world, e);
        for (int32_t i = 0; i < kids.count && s.n < s.cap; ++i) {
            const flecs::entity kid(*s.world, kids.ids[i]);
            if (!kid.has<UiOverlay>()) { // overlays are roots of their own and painted by guiEmit
                emitEntity(s, kid, depth + 1);
            }
        }
        return;
    }
    e.children([&](flecs::entity child) {
        if (s.n < s.cap && !child.has<UiOverlay>()) {
            emitEntity(s, child, depth + 1);
        }
    });
}

void emitEntity(Sink& s, flecs::entity e, uint32_t depth) {
    if (s.n >= s.cap || e.has<UiHidden>()) {
        return;
    }
    const UiBox* box = e.try_get<UiBox>();
    if (box == nullptr) {
        emitChildren(s, e, depth);
        return;
    }
    const UiClip* clipPtr = e.try_get<UiClip>();
    const UiClip clip = clipPtr != nullptr ? *clipPtr : UiClip{box->x, box->y, box->w, box->h};
    if (!clipEmpty(clip)) {
        if (const UiPaint* paint = e.try_get<UiPaint>()) {
            float rgba[4];
            const float radius = std::max(0.0f, paint->radius);
            paletteColor(*s.sheet, paint->token, paint->tint, rgba);
            const bool spriteOnly = paint->photo != kGuiNoPhoto && paint->token == kTokCustom;
            if (!spriteOnly) {
                emitRect(s, box->x, box->y, box->w, box->h, rgba, radius, clip);
            }
            if (const UiDraggable* drag = e.try_get<UiDraggable>(); drag != nullptr && drag->axis == 0 && drag->flat == 0) {
                float fill[4];
                guiStyleColor(*s.sheet, kTokAccent, fill);
                fill[3] = kGuiDragFillAlpha;
                emitRect(s, box->x, box->y, box->w * std::clamp(drag->value, 0.0f, 1.0f), box->h, fill, radius, clip);
            }
            if (paint->photo != kGuiNoPhoto) {
                emitPhoto(s, *box, paint->photo, paint->tint, clip);
            }
            if (paint->borderW > 0.5f) {
                const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                paletteColor(*s.sheet, paint->borderToken, one, rgba);
                emitStroke(s, *box, rgba, radius, paint->borderW, clip);
            }
        }
        if (const UiText* text = e.try_get<UiText>()) {
            emitText(s, e, *box, *text, clip);
        }
    }
    emitChildren(s, e, depth);
}

} // namespace

uint32_t guiEmit(flecs::world& world, const GuiStyleSheet& sheet, UiPrimitive* out, uint32_t cap,
                 const GuiInteractState* interact) {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    Sink sink;
    sink.world = &world;
    sink.sheet = &sheet;
    sink.interact = interact;
    sink.out = out;
    sink.cap = cap;
    const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    paletteColor(sheet, kTokText, one, sink.text);
    paletteColor(sheet, kTokAccent, one, sink.select);
    sink.select[3] = 0.35f;

    struct RootKey {
        flecs::entity entity;
        uint32_t order; // 0 = ordinary root, layer + 1 = overlay root
    };
    RootKey roots[kGuiEmitRootCap];
    uint32_t count = 0;
    rootQuery(world).roots.each([&](flecs::entity e) {
        if (count >= kGuiEmitRootCap) {
            return;
        }
        const UiOverlay* overlay = e.try_get<UiOverlay>();
        roots[count++] = RootKey{e, overlay != nullptr ? static_cast<uint32_t>(overlay->layer) + 1u : 0u};
    });
    for (uint32_t i = 1; i < count; ++i) { // stable insertion sort, no allocation
        const RootKey key = roots[i];
        uint32_t j = i;
        for (; j > 0 && roots[j - 1].order > key.order; --j) {
            roots[j] = roots[j - 1];
        }
        roots[j] = key;
    }
    for (uint32_t i = 0; i < count && sink.n < sink.cap; ++i) {
        emitEntity(sink, roots[i].entity, 0);
    }
    return sink.n;
}

} // namespace burnhope::gui
