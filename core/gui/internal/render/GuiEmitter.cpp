#include "render/GuiEmitter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string_view>

#include "gui/GuiIcons.hpp"
#include "layout/GuiDock.hpp"
#include "gui/GuiRow.hpp"
#include "interaction/InteractScroll.hpp"
#include "interaction/InteractTextEdit.hpp"
#include "text/GuiTextEngine.hpp"

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

// A node paints only where its own box meets the clip it inherited; a box wholly outside the clip is culled.
bool boxVisible(const UiBox& b, const UiClip& c) {
    return !clipEmpty(c) && b.x < c.x + c.w && b.x + b.w > c.x && b.y < c.y + c.h && b.y + b.h > c.y;
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

void emitPhoto(Sink& s, const UiBox& b, uint8_t slot, const float tint[4], const UiClip& clip, const float uv[4]) {
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
    std::copy_n(uv, 4, p->extra);
    putClip(*p, clip, true);
}

// Tile metrics come from the "tile" group of the style sheet's tokens (assets/ui/styles.json); a missing token reads 0.
float tileMetric(const GuiStyleSheet& sheet, std::string_view name) {
    float v = 0.0f;
    return guiStyleNumber(sheet, name, v) ? v : 0.0f;
}

// A photo of a paint: the whole slot, one cell of its atlas grid, or (tile mode) that cell as a square at the top of the box.
void emitPaintPhoto(Sink& s, const UiBox& box, const UiPaint& paint, const UiClip& clip) {
    float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    UiBox target = box;
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float* tint = paint.tint;
    if (paint.cols != 0 && paint.rows != 0) {
        const float du = 1.0f / static_cast<float>(paint.cols);
        const float dv = 1.0f / static_cast<float>(paint.rows);
        uv[0] = static_cast<float>(paint.cell % paint.cols) * du;
        uv[1] = static_cast<float>(paint.cell / paint.cols) * dv;
        uv[2] = uv[0] + du;
        uv[3] = uv[1] + dv;
    }
    if (paint.tile != 0) {
        const float side = std::min(static_cast<float>(paint.tile), box.w);
        target = UiBox{box.x + (box.w - side) * 0.5f, box.y + tileMetric(*s.sheet, "tile.top"), side, side};
        tint = white;
    }
    emitPhoto(s, target, paint.photo, tint, clip, uv);
}

// One glyph of the icon atlas (photo slot kPhotoIconAtlas): a white mask tinted by `rgba`, centred in the box.
void emitIcon(Sink& s, const UiBox& b, uint8_t cell, bool trailing, float want, const float rgba[4], const UiClip& clip) {
    const float side = std::min(want, std::min(b.w, b.h));
    if (side < 1.0f || rgba[3] <= 0.0f) {
        return;
    }
    UiPrimitive* p = take(s);
    if (p == nullptr) {
        return;
    }
    constexpr float kCell = 1.0f / static_cast<float>(kGuiIconCells);
    const float u0 = static_cast<float>(cell % kGuiIconCells) * kCell;
    const float v0 = static_cast<float>(cell / kGuiIconCells) * kCell;
    p->posX = trailing ? b.x + b.w - side - kGuiIconTrailPad : b.x + (b.w - side) * 0.5f;
    p->posY = b.y + (b.h - side) * 0.5f;
    p->sizeX = side;
    p->sizeY = side;
    std::copy_n(rgba, 4, p->color);
    p->texId = kPhotoIconAtlas;
    p->flags = kUiFlagPhoto;
    p->extra[0] = u0;
    p->extra[1] = v0;
    p->extra[2] = u0 + kCell;
    p->extra[3] = v0 + kCell;
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

// single: one line, never wrapped; text wider than the box is cut with a trailing "..." (list / tree / table rows).
void emitText(Sink& s, flecs::entity e, const UiBox& box, const UiText& t, const UiClip& clip, bool single) {
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
    const bool wrap = !single && edit == nullptr && area.h >= lineH * 1.5f;
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
        const bool cut = single && edit == nullptr && line.width > area.w;
        float pen = originX;
        if (cut) { // overflowing text never centres or right-aligns: it anchors at the left edge and ends in "..."
            pen = area.x;
        } else if (edit == nullptr && t.align == 1) {
            pen = area.x + (area.w - line.width) * 0.5f;
        } else if (edit == nullptr && t.align == 2) {
            pen = area.x + area.w - line.width;
        }
        const float baseline = lineTop + m.ascent * px;
        const std::string_view run = str.substr(line.begin, line.end - line.begin);
        const float limit = area.x + area.w;
        GuiGlyph dot{};
        const float dotsW = cut && guiTextGlyph(m, static_cast<uint32_t>('.'), dot) ? 3.0f * dot.advance * px : 0.0f;
        bool ellipsis = false;
        for (uint32_t at = 0; at < run.size() && s.n < s.cap;) {
            uint32_t step = 0;
            const uint32_t cp = guiTextDecode(run, at, step);
            if (step == 0) {
                break;
            }
            at += step;
            if (cut && guiTextGlyph(m, cp, unknown) && pen + unknown.advance * px + dotsW > limit) {
                ellipsis = true;
                break;
            }
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
        for (uint32_t d = 0; ellipsis && d < 3 && s.n < s.cap; ++d) {
            const int slot = guiTextSlot(static_cast<uint32_t>('.'));
            UiPrimitive* p = take(s);
            if (p == nullptr) {
                break;
            }
            p->posX = pen + dot.bearingX * px;
            p->posY = baseline - dot.bearingY * px;
            p->sizeX = dot.w * px;
            p->sizeY = dot.h * px;
            std::copy_n(s.text, 4, p->color);
            p->texId = static_cast<uint32_t>(slot < 0 ? 0 : slot);
            p->flags = kUiFlagGlyph;
            putClip(*p, clip, false);
            pen += dot.advance * px;
        }
    }

    if (focused && (s.interact->caretTick % 60u) < 30u) {
        const uint32_t caret = std::min<uint32_t>(edit->caret, t.len);
        const float cx = originX + guiTextXFromCaret(m, str, px, caret);
        emitRect(s, cx, top, 1.5f, lineH, s.text, 0.0f, clip);
    }
}

// ---------------------------------------------------------------- list / tree rows
void emitRun(Sink& s, flecs::entity e, const UiBox& box, std::string_view run, uint8_t align, uint8_t size, const UiClip& clip) {
    if (run.empty()) {
        return;
    }
    UiText part{};
    const size_t n = std::min<size_t>(run.size(), kGuiTextCap - 1);
    std::copy_n(run.data(), n, part.text);
    part.len = static_cast<uint8_t>(n);
    part.align = align;
    part.size = size;
    emitText(s, e, box, part, clip, true);
}

// A row with typed fields: indent, chevron column, icon, the name (cut with "..." at its column) and the date / size columns.
void emitRow(Sink& s, flecs::entity e, const UiBox& box, const UiText& t, const GuiRow& r, const UiClip& clip) {
    const float padL = textArea(e, box).x - box.x;
    const bool columns = r.col1[0] != '\0' || r.col2[0] != '\0';
    const float nameEnd = box.x + (columns ? box.w * 0.60f : box.w);
    float pen = box.x + static_cast<float>(r.depth) * kGuiRowIndentPx;
    if ((r.flags & kGuiRowTree) != 0) {
        if (r.chevron != 0) {
            emitIcon(s, UiBox{pen + padL, box.y, kGuiRowCellPx - 4.0f, box.h}, r.chevron, false, kGuiIconDrawPx, s.text, clip);
        }
        pen += kGuiRowCellPx;
    }
    if (r.icon != 0) {
        emitIcon(s, UiBox{pen + padL, box.y, kGuiRowCellPx - 4.0f, box.h}, r.icon, false, kGuiIconDrawPx, s.text, clip);
        pen += kGuiRowCellPx;
    }
    emitRun(s, e, UiBox{pen, box.y, std::max(0.0f, nameEnd - pen), box.h}, std::string_view(t.text, t.len), 0, t.size, clip);
    emitRun(s, e, UiBox{box.x + box.w * 0.60f, box.y, box.w * 0.24f, box.h}, r.col1, 0, t.size, clip);
    emitRun(s, e, UiBox{box.x + box.w * 0.84f, box.y, box.w * 0.16f, box.h}, r.col2, 2, t.size, clip);
}

// Empty, unfocused editable: its hint in the muted colour.
void emitPlaceholder(Sink& s, flecs::entity e, const UiBox& box, const UiText& t, const UiPlaceholder& hint, const UiClip& clip) {
    float muted[4];
    const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    paletteColor(*s.sheet, kTokMuted, one, muted);
    float saved[4];
    std::copy_n(s.text, 4, saved);
    std::copy_n(muted, 4, s.text);
    UiText part{};
    std::copy_n(hint.text, sizeof(hint.text), part.text);
    part.len = static_cast<uint8_t>(std::char_traits<char>::length(hint.text));
    part.size = t.size;
    emitText(s, e, box, part, clip, false);
    std::copy_n(saved, 4, s.text);
}

// File tile caption: "name\ntype" - the name in the text colour, the type underneath in the muted colour and a smaller size.
void emitTileCaption(Sink& s, flecs::entity e, const UiBox& box, const UiText& t, const UiClip& clip) {
    const std::string_view full(t.text, t.len);
    const size_t cut = full.find('\n');
    const float pad = tileMetric(*s.sheet, "tile.pad");
    const float nameH = tileMetric(*s.sheet, "tile.nameH");
    const float typeH = tileMetric(*s.sheet, "tile.typeH");
    const uint8_t typePx = static_cast<uint8_t>(tileMetric(*s.sheet, "tile.typePx"));
    // The caption is scissored to its own cell, so no glyph can reach a sibling tile.
    UiClip cell = clip;
    cell.x = std::max(clip.x, box.x);
    cell.y = std::max(clip.y, box.y);
    cell.w = std::max(0.0f, std::min(clip.x + clip.w, box.x + box.w) - cell.x);
    cell.h = std::max(0.0f, std::min(clip.y + clip.h, box.y + box.h) - cell.y);
    if (clipEmpty(cell)) {
        return;
    }
    const float innerW = std::max(0.0f, box.w - pad * 2.0f);
    emitRun(s, e, UiBox{box.x + pad, box.y, innerW, nameH}, full.substr(0, cut), t.align, t.size, cell);
    if (cut == std::string_view::npos) {
        return;
    }
    float muted[4];
    const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    paletteColor(*s.sheet, kTokMuted, one, muted);
    float saved[4];
    std::copy_n(s.text, 4, saved);
    std::copy_n(muted, 4, s.text);
    emitRun(s, e, UiBox{box.x + pad, box.y + nameH - 2.0f, innerW, typeH}, full.substr(cut + 1), t.align, typePx, cell);
    std::copy_n(saved, 4, s.text);
}

// Track and thumb of an overflowing scroll view, painted over its content: the thumb is grey, orange while the
// pointer is over the right band or the thumb is dragged.
void emitScrollBar(Sink& s, flecs::entity e, const UiBox& box, const UiClip& clip) {
    const UiScroll* scroll = e.has<UiScrollable>() ? e.try_get<UiScroll>() : nullptr;
    if (scroll == nullptr) {
        return;
    }
    const GuiBarGeometry g = guiScrollBar(box, *scroll);
    if (g.range <= 0.0f) {
        return;
    }
    float track[4];
    float thumb[4];
    guiStyleColor(*s.sheet, kTokChrome, track);
    track[3] = 0.55f;
    const bool hot = s.interact != nullptr &&
                     (s.interact->barDrag == e.id() || (s.interact->pointerX >= box.x + box.w - kGuiBarHitW && s.interact->pointerX < box.x + box.w &&
                                                        s.interact->pointerY >= box.y && s.interact->pointerY < box.y + box.h));
    guiStyleColor(*s.sheet, hot ? kTokAccent : kTokMuted, thumb);
    emitRect(s, g.trackX, g.trackY, kGuiBarW, g.trackH, track, kGuiBarW * 0.5f, clip);
    emitRect(s, g.trackX, g.thumbY, kGuiBarW, g.thumbH, thumb, kGuiBarW * 0.5f, clip);
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
    const bool visible = boxVisible(*box, clip);
    if (visible) {
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
                emitPaintPhoto(s, *box, *paint, clip);
            }
            if (paint->icon != 0) {
                float glyph[4];
                const float white[4] = {paint->tint[0], paint->tint[1], paint->tint[2], 1.0f};
                paletteColor(*s.sheet, kTokText, white, glyph);
                const UiText* caption = e.try_get<UiText>();
                const bool captioned = caption != nullptr && caption->len != 0;
                // An icon button keeps kGuiIconPad around the glyph, so a bigger button shows a bigger icon.
                const float want = captioned ? kGuiIconDrawPx : std::clamp(std::min(box->w, box->h) - 2.0f * kGuiIconPad, kGuiIconDrawPx - 4.0f, kGuiIconMaxPx);
                if (e.has<UiTab>()) { // a tab's icon leads the title, the close button owns the trailing edge
                    emitIcon(s, UiBox{box->x + kGuiTabIconX, box->y, want, box->h}, paint->icon, false, want, glyph, clip);
                } else {
                    emitIcon(s, *box, paint->icon, captioned, want, glyph, clip);
                }
            }
            if (paint->borderW > 0.5f) {
                const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                paletteColor(*s.sheet, paint->borderToken, one, rgba);
                emitStroke(s, *box, rgba, radius, paint->borderW, clip);
            }
        }
        if (const UiText* text = e.try_get<UiText>()) {
            const UiPaint* paint = e.try_get<UiPaint>();
            if (paint != nullptr && paint->tile != 0) { // a file tile: the caption starts under the thumbnail
                const float used = std::min(box->h, static_cast<float>(paint->tile) + tileMetric(*s.sheet, "tile.top") * 1.5f);
                emitTileCaption(s, e, UiBox{box->x, box->y + used, box->w, box->h - used}, *text, clip);
            } else if (const GuiRow* row = e.try_get<GuiRow>(); row != nullptr && guiRowActive(*row)) {
                emitRow(s, e, *box, *text, *row, clip);
            } else if (const UiPlaceholder* hint = text->len == 0 && !e.has<UiFocused>() ? e.try_get<UiPlaceholder>() : nullptr) {
                emitPlaceholder(s, e, *box, *text, *hint, clip);
            } else {
                emitText(s, e, *box, *text, clip, e.has<UiVirtualItem>());
            }
        }
    }
    emitChildren(s, e, depth);
    if (visible) {
        emitScrollBar(s, e, *box, clip);
    }
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
