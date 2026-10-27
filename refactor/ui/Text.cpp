#include "Str.hpp"
#include "ui/Text.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"

#include <bit>
#include <cstring>

namespace burnhope {
namespace {

void putFloat(uint32_t& dst, float v) noexcept {
    dst = std::bit_cast<uint32_t>(v);
}

struct Cp {
    uint8_t at = 0;
    uint8_t n = 1;
    float adv = 0.0f;
    uint8_t kind = 0;
};

uint32_t foldCss(uint32_t cp, UiTextTransform mode, bool capNext) noexcept {
    auto upper = [](uint32_t c) noexcept -> uint32_t {
        if (c >= 'a' && c <= 'z') {
            return c - 32;
        }
        if (c >= 0x0430 && c <= 0x044F) {
            return c - 0x20;
        }
        if (c == 0x0451) {
            return 0x0401;
        }
        return c;
    };
    auto lower = [](uint32_t c) noexcept -> uint32_t {
        if (c >= 'A' && c <= 'Z') {
            return c + 32;
        }
        if (c >= 0x0410 && c <= 0x042F) {
            return c + 0x20;
        }
        if (c == 0x0401) {
            return 0x0451;
        }
        return c;
    };
    if (mode == UiTextTransform::Uppercase) {
        return upper(cp);
    }
    if (mode == UiTextTransform::Lowercase) {
        return lower(cp);
    }
    if (mode == UiTextTransform::Capitalize && capNext) {
        return upper(cp);
    }
    return cp;
}

void pushCp(char* dst, uint8_t& n, uint32_t cp) noexcept {
    if (cp < 0x80) {
        if (n < 95) {
            dst[n++] = static_cast<char>(cp);
        }
        return;
    }
    if (cp < 0x800) {
        if (n < 94) {
            dst[n++] = static_cast<char>(0xC0 | (cp >> 6));
            dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        return;
    }
    if (n < 93) {
        dst[n++] = static_cast<char>(0xE0 | (cp >> 12));
        dst[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void copyRange(TextRun& run, const char* src, const Cp* cp, int from, int to, bool dots, float width) noexcept {
    if (run.lines >= kUiLineCap) {
        return;
    }
    TextLine& line = run.line[run.lines];
    line.begin = run.len;
    for (int k = from; k < to; ++k) {
        for (uint8_t t = 0; t < cp[k].n && run.len < 95; ++t) {
            run.bytes[run.len++] = src[cp[k].at + t];
        }
    }
    if (dots && run.len <= 92) {
        run.bytes[run.len++] = '.';
        run.bytes[run.len++] = '.';
        run.bytes[run.len++] = '.';
    }
    line.len = static_cast<uint8_t>(run.len - line.begin);
    line.w = width;
    ++run.lines;
}

void ellipsisEnd(TextRun& run, const char* src, const Cp* cp, const float* ps, int from, int to, float budget, float dots) noexcept {
    int keep = from;
    float acc = 0.0f;
    if (budget > 1.0f) {
        for (int k = from; k < to; ++k) {
            if (acc + cp[k].adv > budget) {
                break;
            }
            acc += cp[k].adv;
            keep = k + 1;
        }
    }
    const float width = (keep > from ? ps[keep] - ps[from] : 0.0f) + dots;
    copyRange(run, src, cp, from, keep, true, width);
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
    const float em = ui.look.lineH();
    float fit = emScale > 0.05f ? emScale : 1.0f;
    if (emScale <= 0.05f && em > box.h && em > 0.5f) {
        fit = box.h / em;
    }
    const float used = scale * fit;
    const float baseline = box.y + (box.h - em * fit) * 0.5f + em * fit * 0.84f;
    const float textW = uiMeasure(ui, text, len, len) * fit;
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
        pen += adv + tracking;
        if (cp == static_cast<uint32_t>(' ')) {
            pen += spaceGap;
        }
        i = static_cast<uint8_t>(i + step);
        if (gw < 0.5f || gh < 0.5f || x >= box.x + box.w || x + gw <= box.x) {
            continue;
        }
        UiPrimitive& prim = dst[n++];
        prim = {};
        prim.posX = x;
        prim.posY = y;
        prim.sizeX = gw;
        prim.sizeY = gh;
        prim.color[0] = r;
        prim.color[1] = g;
        prim.color[2] = b;
        prim.color[3] = alpha;
        prim.texId = static_cast<uint32_t>(gi);
        prim.flags = kUiFlagGlyph;
        if (clip != nullptr) {
            prim.flags |= kUiFlagClip;
            prim.extra[1] = clip->x;
            prim.extra[2] = clip->y;
            prim.extra[3] = clip->w;
            putFloat(prim.pad0, clip->h);
        }
    }
    return n;
}

void measureLabel(UiState& s, uint16_t id, float maxW, float& outW, float& outH, void*) {
    outW = 0.0f;
    outH = s.look.lineH();
    const UiText* text = id < s.count ? s.ent[id].try_get<UiText>() : nullptr;
    if (text == nullptr || text->len == 0) {
        return;
    }
    if (text->wrap && maxW > 1.0f) {
        const UiBox slot{0.0f, 0.0f, maxW, 100000.0f};
        const TextRun run = textRun(s, *text, slot);
        outW = maxW;
        if (run.box.h > 1.0f) {
            outH = run.box.h;
        }
        return;
    }
    outW = uiMeasure(s, text->bytes, text->len, text->len);
}

} // namespace

float uiAdvance(const UiState& s, uint32_t cp) {
    int gi = uiGlyphIndex(cp);
    if (gi < 0 || gi >= static_cast<int>(kUiFontGlyphs)) {
        gi = static_cast<int>('?' - 32);
    }
    const float scale = s.font.ready && s.font.fontPx > 1.0f ? s.look.glyphH / s.font.fontPx : 1.0f;
    return s.font.ready ? s.font.glyphs[gi].advance * scale : s.look.advance;
}

float uiMeasure(const UiState& s, const char* text, uint8_t len, uint8_t until) {
    float w = 0.0f;
    uint8_t i = 0;
    const uint8_t end = until < len ? until : len;
    while (i < end) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(text, len, i, step);
        if (step == 0) {
            break;
        }
        w += uiAdvance(s, cp);
        i = static_cast<uint8_t>(i + step);
    }
    return w;
}

void textInk(const UiLook& look, const UiPaintStyle& style, float r, float g, float b, bool hot, float& tr, float& tg, float& tb) {
    constexpr float kLumaR = 0.30f; // Rec.601 luma weights
    constexpr float kLumaG = 0.59f;
    constexpr float kLumaB = 0.11f;
    const float lum = kLumaR * r + kLumaG * g + kLumaB * b;
    const float* ink = lum > style.inkThreshold ? look.window : look.text;
    tr = ink[0];
    tg = ink[1];
    tb = ink[2];
    if (hot) {
        tr = tr + style.hoverLift > 1.0f ? 1.0f : tr + style.hoverLift;
        tg = tg + style.hoverLift > 1.0f ? 1.0f : tg + style.hoverLift;
        tb = tb + style.hoverLift > 1.0f ? 1.0f : tb + style.hoverLift;
    }
}

void uiText(UiState& s, uint16_t id, const char* text) {
    if (id >= s.count) {
        return;
    }
    char bytes[96]{};
    const uint8_t n = uiStrCopy(bytes, text);
    UiText created{};
    if (const UiText* live = s.ent[id].try_get<UiText>()) {
        if (live->len == n && std::memcmp(live->bytes, bytes, static_cast<size_t>(n) + 1u) == 0) {
            return;
        }
        created = *live;
    }
    std::memcpy(created.bytes, bytes, static_cast<size_t>(n) + 1u);
    created.len = n;
    s.ent[id].set<UiText>(created);
    if (!s.ent[id].has<UiContentMeasure>()) {
        s.ent[id].set<UiContentMeasure>(UiContentMeasure{measureLabel, nullptr});
    }
    s.visualDirty = true;
    if (auto* own = s.ent[id].try_get_mut<UiFlex>(); own != nullptr && own->widthMode == UiSize::Fit) {
        float padL = 0.0f;
        float padR = 0.0f;
        float padT = 0.0f;
        float padB = 0.0f;
        uiPadOf(*own, padL, padR, padT, padB);
        own->width = uiMeasure(s, bytes, n, n) + padL + padR;
        s.layoutDirty = true;
    }
    const uint16_t parent = s.parentOf[id];
    const UiFlex* parentFlex = parent < s.count ? s.ent[parent].try_get<UiFlex>() : nullptr;
    const bool column = parentFlex != nullptr
        && (parentFlex->direction == UiFlexDir::Column || parentFlex->direction == UiFlexDir::ColumnReverse);
    if (!column) {
        s.layoutDirty = true;
    }
}

TextRun textRun(const UiState& s, const UiText& text, const UiBox& slot) {
    TextRun run{};
    run.box = slot;
    run.deco = text.deco;
    run.scale = 1.0f;
    if (text.len == 0 || slot.w < 1.0f || slot.h < 1.0f) {
        run.box.w = 0.0f;
        return run;
    }
    const float em = s.look.lineH();
    float scale = text.size > 0.05f ? text.size : 1.0f;
    if (em * scale > slot.h && em > 0.5f) {
        scale = slot.h / em;
    }
    run.scale = scale;
    run.tracking = text.tracking;
    run.wordGap = text.wordGap;
    run.thick = text.thick > 0.4f ? text.thick : 1.0f;
    run.under = text.under;
    run.shadowX = text.shadowX;
    run.shadowY = text.shadowY;

    char raw[96]{};
    uint8_t rawLen = 0;
    bool capNext = text.transform == UiTextTransform::Capitalize;
    const int tabN = text.tabs == 0 ? 4 : static_cast<int>(text.tabs);
    uint8_t at = 0;
    while (at < text.len && rawLen < 95) {
        uint8_t step = 1;
        uint32_t code = uiReadUtf8(text.bytes, text.len, at, step);
        if (step == 0) {
            break;
        }
        if (code == static_cast<uint32_t>('\t')) {
            for (int t = 0; t < tabN && rawLen < 95; ++t) {
                raw[rawLen++] = ' ';
            }
            capNext = false;
        } else {
            code = foldCss(code, text.transform, capNext);
            capNext = code == static_cast<uint32_t>(' ') || code == static_cast<uint32_t>('\n');
            pushCp(raw, rawLen, code);
        }
        at = static_cast<uint8_t>(at + step);
    }
    raw[rawLen] = '\0';

    Cp cp[96]{};
    int ncp = 0;
    uint8_t i = 0;
    while (i < rawLen && ncp < 96) {
        uint8_t step = 1;
        const uint32_t code = uiReadUtf8(raw, rawLen, i, step);
        if (step == 0) {
            break;
        }
        uint8_t kind = 0;
        if (code == static_cast<uint32_t>('\n')) {
            kind = 1;
        } else if (code == static_cast<uint32_t>(' ')) {
            kind = 2;
        }
        float adv = uiAdvance(s, code) * scale + text.tracking;
        if (kind == 2) {
            adv += text.wordGap;
        }
        cp[ncp] = {i, step, adv, kind};
        i = static_cast<uint8_t>(i + step);
        ++ncp;
    }
    float ps[97]{};
    for (int k = 0; k < ncp; ++k) {
        ps[k + 1] = ps[k] + cp[k].adv;
    }
    const float dots = uiMeasure(s, "...", 3, 3) * scale + text.tracking * 3.0f;
    const float full = ps[ncp];
    const float firstW = text.indent > 0.0f && text.indent < slot.w ? slot.w - text.indent : slot.w;

    if (!text.wrap) {
        if (text.ellipsis != UiTextEllipsis::None && full > firstW) {
            if (text.ellipsis == UiTextEllipsis::Middle) {
                int gapL = ncp / 2;
                int gapR = gapL;
                while ((ps[gapL] + dots + (ps[ncp] - ps[gapR])) > firstW && (gapL > 0 || gapR < ncp)) {
                    if (gapL >= ncp - gapR && gapL > 0) {
                        --gapL;
                    } else if (gapR < ncp) {
                        ++gapR;
                    } else {
                        break;
                    }
                }
                const float width = ps[gapL] + dots + (ps[ncp] - ps[gapR]);
                TextLine& line = run.line[0];
                line.begin = 0;
                for (int k = 0; k < gapL && run.len < 95; ++k) {
                    for (uint8_t t = 0; t < cp[k].n && run.len < 95; ++t) {
                        run.bytes[run.len++] = raw[cp[k].at + t];
                    }
                }
                if (run.len <= 92) {
                    run.bytes[run.len++] = '.';
                    run.bytes[run.len++] = '.';
                    run.bytes[run.len++] = '.';
                }
                for (int k = gapR; k < ncp && run.len < 95; ++k) {
                    for (uint8_t t = 0; t < cp[k].n && run.len < 95; ++t) {
                        run.bytes[run.len++] = raw[cp[k].at + t];
                    }
                }
                line.len = run.len;
                line.w = width;
                run.lines = 1;
            } else {
                ellipsisEnd(run, raw, cp, ps, 0, ncp, firstW - dots, dots);
            }
        } else {
            copyRange(run, raw, cp, 0, ncp, false, full);
        }
    } else {
        int cursor = 0;
        while (cursor < ncp && run.lines < kUiLineCap) {
            if (cp[cursor].kind == 1) {
                copyRange(run, raw, cp, cursor, cursor, false, 0.0f);
                ++cursor;
                continue;
            }
            uint8_t kind[96]{};
            for (int k = 0; k < ncp; ++k) {
                kind[k] = cp[k].kind;
            }
            const float limit = run.lines == 0 ? firstW : slot.w;
            const UiWrapCut cut = uiWrapCut(ps, kind, ncp, cursor, limit, text.breakAll ? 1 : 0);
            const int end = cut.end;
            const int next = cut.next;
            const bool more = next < ncp;
            const bool lastSlot = static_cast<uint8_t>(run.lines + 1) >= kUiLineCap;
            if (lastSlot && more && text.ellipsis != UiTextEllipsis::None) {
                ellipsisEnd(run, raw, cp, ps, cursor, ncp, (run.lines == 0 ? firstW : slot.w) - dots, dots);
                break;
            }
            copyRange(run, raw, cp, cursor, end, false, ps[end] - ps[cursor]);
            cursor = next;
        }
    }

    if (text.align == UiTextAlign::Justify) {
        for (uint8_t li = 0; li < run.lines; ++li) {
            const bool last = static_cast<uint8_t>(li + 1) == run.lines;
            if (text.wrap && last) {
                continue;
            }
            int spaces = 0;
            const TextLine& row = run.line[li];
            for (uint8_t k = 0; k < row.len; ++k) {
                if (run.bytes[row.begin + k] == ' ') {
                    ++spaces;
                }
            }
            float room = slot.w - row.w;
            if (li == 0 && text.indent > 0.0f) {
                room -= text.indent;
            }
            if (spaces > 0 && room > 0.5f) {
                run.line[li].gap = room / static_cast<float>(spaces);
                run.line[li].w += room;
            }
        }
    }

    const float lineH = em * scale;
    const float gap = text.leading > 0.0f ? text.leading : 0.0f;
    const float block = run.lines > 0
        ? static_cast<float>(run.lines) * lineH + static_cast<float>(run.lines - 1) * gap
        : 0.0f;
    float y = slot.y;
    const float shift = text.wrap ? text.scroll : 0.0f;
    if (text.valign == UiTextValign::Bottom && block < slot.h) {
        y = slot.y + slot.h - block;
    } else if (text.valign == UiTextValign::Center && block < slot.h) {
        y = slot.y + (slot.h - block) * 0.5f;
    }
    y -= shift;
    const float xScroll = text.wrap ? 0.0f : text.scroll;
    for (uint8_t line = 0; line < run.lines; ++line) {
        TextLine& row = run.line[line];
        float x = slot.x - (text.align == UiTextAlign::Left || text.align == UiTextAlign::Justify ? xScroll : 0.0f);
        if (text.align == UiTextAlign::Center && row.w < slot.w) {
            x = slot.x + (slot.w - row.w) * 0.5f;
        } else if (text.align == UiTextAlign::Right && row.w < slot.w) {
            x = slot.x + slot.w - row.w;
        }
        if (line == 0 && text.indent > 0.0f && (text.align == UiTextAlign::Left || text.align == UiTextAlign::Justify)) {
            x += text.indent;
        }
        row.x = x;
        row.y = y;
        row.h = lineH;
        y += lineH + gap;
    }
    run.bytes[run.len < 95 ? run.len : 95] = '\0';
    if (run.lines > 0) {
        run.box.x = run.line[0].x;
        run.box.y = run.line[0].y;
        run.box.w = run.line[0].w + 1.0f;
        run.box.h = block > 0.0f ? block : lineH;
    }
    return run;
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
        const bool under = (run.deco & UiTextDeco::Underline);
        const bool strike = (run.deco & UiTextDeco::Strikethrough);
        const bool over = (run.deco & UiTextDeco::Overline);
        if (under && line.w > 1.0f) {
            n = emitRect(dst, n, cap, line.x, line.y + line.h - thick - run.under, line.w, thick, r, g, b, a, 0.0f, clip);
        }
        if (strike && line.w > 1.0f) {
            n = emitRect(dst, n, cap, line.x, line.y + line.h * 0.45f, line.w, thick, r, g, b, a, 0.0f, clip);
        }
        if (over && line.w > 1.0f) {
            n = emitRect(dst, n, cap, line.x, line.y + 1.0f, line.w, thick, r, g, b, a, 0.0f, clip);
        }
    }
    return n;
}

uint32_t uiEmitTextNode(
    UiState& s,
    uint16_t id,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox* clip,
    float r,
    float g,
    float b,
    float a) {
    const UiText* text = id < s.count ? s.ent[id].try_get<UiText>() : nullptr;
    if (text == nullptr || text->len == 0) {
        return n;
    }
    UiBox slot = s.box[id];
    if (const UiFlex* flex = s.ent[id].try_get<UiFlex>()) {
        const bool sides = flex->padL > 0.0f || flex->padR > 0.0f || flex->padT > 0.0f || flex->padB > 0.0f;
        const float left = sides ? flex->padL : flex->pad;
        const float right = sides ? flex->padR : flex->pad;
        const float top = sides ? flex->padT : flex->pad;
        const float bottom = sides ? flex->padB : flex->pad;
        if (slot.w > left + right + 1.0f && slot.h > top + bottom + 1.0f) {
            slot.x += left;
            slot.y += top;
            slot.w -= left + right;
            slot.h -= top + bottom;
        }
    }
    if (clip != nullptr) {
        const bool empty = clip->w < 0.5f || clip->h < 0.5f;
        const bool miss = slot.x >= clip->x + clip->w || clip->x >= slot.x + slot.w
            || slot.y >= clip->y + clip->h || clip->y >= slot.y + slot.h;
        if (empty || miss) {
            return n;
        }
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    float tr = r;
    float tg = g;
    float tb = b;
    float ta = a;
    const bool bare = paint != nullptr && (paint->token == UiToken::Text || paint->a <= kUiInvisibleAlpha);
    if (bare && paint->token == UiToken::Custom) {
        tr = paint->r;
        tg = paint->g;
        tb = paint->b;
        ta = 1.0f;
    } else if (bare) {
        tr = s.look.text[0];
        tg = s.look.text[1];
        tb = s.look.text[2];
        ta = 1.0f;
    } else {
        textInk(s.look, s.paintStyle, r, g, b, id == s.hovered, tr, tg, tb);
    }
    const TextRun run = textRun(s, *text, slot);
    if (run.lines == 0) {
        return n;
    }
    return uiEmitRun(s, dst, n, cap, run, tr, tg, tb, ta, clip);
}

} // namespace burnhope
