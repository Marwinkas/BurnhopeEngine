#include "ui/Bar.hpp"
#include "ui/Icon.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Field.hpp"
#include "ui/State.hpp"

#include <cstdint>

namespace burnhope {

namespace {

void pushBar(UiState& s, UiNodeBars& bars, float x, float y, float w, float h, float r, float g, float b, float a, float radius, uint32_t flags, float spread) {
    if (w < 0.5f || h < 0.5f) {
        return;
    }
    UiBar bar{};
    bar.x = x;
    bar.y = y;
    bar.w = w;
    bar.h = h;
    bar.r = r;
    bar.g = g;
    bar.b = b;
    bar.a = a;
    bar.radius = radius;
    bar.flags = flags;
    bar.spread = spread;
    bar.on = 1;
    uiNodeBarPush(s, bars, bar);
}

void putChrome(UiState& s, uint16_t id, const UiNodeBars& bars, const UiTextInk& ink) {
    if (auto* live = s.ent[id].try_get_mut<UiNodeBars>()) {
        *live = bars;
    } else if (bars.n != 0 || bars.icon != 0) {
        s.ent[id].set<UiNodeBars>(bars);
    }
    if (auto* live = s.ent[id].try_get_mut<UiTextInk>()) {
        *live = ink;
    } else if (ink.on != 0) {
        s.ent[id].set<UiTextInk>(ink);
    }
}

} // namespace

void fieldPlace(UiState& s) {
    uint16_t fields[kUiCap];
    uint16_t nf = 0;
    s.world.each<UiField>([&](flecs::entity e, UiField&) {
        const UiSlot* slot = e.try_get<UiSlot>();
        if (slot != nullptr && slot->index < s.count && s.ent[slot->index] == e && nf < kUiCap) {
            fields[nf++] = slot->index;
        }
    });
    for (uint16_t at = 0; at < nf; ++at) {
        const uint16_t id = fields[at];
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        const UiField* field = s.ent[id].try_get<UiField>();
        if (paint == nullptr || field == nullptr) {
            continue;
        }
        const UiBox& box = s.box[id];
        UiNodeBars bars{};
        UiTextInk ink{};
        if (field->required != 0 && field->len == 0 && paint->borderW < 0.4f) {
            pushBar(s, bars, box.x, box.y, box.w, box.h,
                s.look.accent[2][0], s.look.accent[2][1], s.look.accent[2][2],
                paint->a > 0.2f ? paint->a : 1.0f, paint->radius, kUiFlagStroke, 1.5f);
        }
        const UiBox inset = fieldInset(s, id, *field);
        const float textW = uiFieldWidth(s, *field, field->len);
        float origin = inset.x - field->scroll;
        if (field->align == 1) {
            origin += (inset.w - textW) * 0.5f;
        } else if (field->align == 2) {
            origin += inset.w - textW;
        }
        const uint8_t sel0 = field->anchor < field->caret ? field->anchor : field->caret;
        const uint8_t sel1 = field->anchor < field->caret ? field->caret : field->anchor;
        if (field->multi != 0) {
            FieldRow rows[16]{};
            const int rn = fieldRows(s, *field, inset.w, rows, 16);
            const float lineH = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
            if (sel0 != sel1) {
                for (int li = 0; li < rn; ++li) {
                    const uint8_t a = sel0 > rows[li].begin ? sel0 : rows[li].begin;
                    uint8_t b = sel1 < rows[li].end ? sel1 : rows[li].end;
                    if (rows[li].begin == rows[li].end && sel0 <= rows[li].begin && sel1 > rows[li].begin) {
                        b = rows[li].begin;
                    }
                    if (a >= b) {
                        continue;
                    }
                    const float x0 = origin + fieldOffset(s, *field, rows[li].begin, a);
                    const float x1 = origin + fieldOffset(s, *field, rows[li].begin, b);
                    const float y = inset.y + static_cast<float>(li) * lineH - field->scroll;
                    if (x1 > x0 && y + lineH > box.y && y < box.y + box.h) {
                        pushBar(s, bars, x0, y, x1 - x0, lineH, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.45f, 2.0f, 0, 0.0f);
                    }
                }
            }
        } else if (sel0 != sel1 && field->len > 0) {
            const float x0 = origin + uiFieldWidth(s, *field, sel0);
            const float x1 = origin + uiFieldWidth(s, *field, sel1);
            const float left = x0 > inset.x ? x0 : inset.x;
            const float right = x1 < inset.x + inset.w ? x1 : inset.x + inset.w;
            if (right > left) {
                pushBar(s, bars, left, box.y + 6.0f, right - left, box.h - 12.0f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.45f, 2.0f, 0, 0.0f);
            }
        }
        if (id == s.focused && paint->disabled == 0 && field->multi != 0) {
            FieldRow rows[16]{};
            const int rn = fieldRows(s, *field, inset.w, rows, 16);
            const float lineH = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
            int line = 0;
            for (int li = 0; li < rn; ++li) {
                if (field->caret >= rows[li].begin && field->caret <= rows[li].end) {
                    line = li;
                }
            }
            const float cx = origin + fieldOffset(s, *field, rows[line].begin, field->caret);
            const float cy = inset.y + static_cast<float>(line) * lineH - field->scroll;
            if (cy + lineH > box.y && cy < box.y + box.h) {
                pushBar(s, bars, cx, cy, 1.5f, lineH, s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, 0.0f, 0, 0.0f);
            }
        } else if (id == s.focused && paint->disabled == 0 && field->scrub == 0) {
            const bool blink = (static_cast<int>(s.time * 1.6f) & 1) == 0;
            const float cx = origin + uiFieldWidth(s, *field, field->caret);
            if (blink && cx >= inset.x - 1.0f && cx <= inset.x + inset.w) {
                pushBar(s, bars, cx, box.y + 7.0f, 1.5f, box.h - 14.0f, s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, 0.0f, 0, 0.0f);
            }
            const UiIme* ime = s.ent[id].try_get<UiIme>();
            if (ime != nullptr && ime->len > 0) {
                const float mark = s.look.advance * static_cast<float>(ime->len);
                pushBar(s, bars, cx, box.y + box.h - 10.0f, mark, 2.0f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.9f, 0.0f, 0, 0.0f);
            }
        }
        if (field->clear != 0 && field->len > 0) {
            constexpr float kSide = 12.0f;
            bars.icon = static_cast<uint8_t>(UiIcon::Dismiss);
            bars.iconX = box.x + box.w - field->padR - kSide;
            bars.iconY = box.y + (box.h - kSide) * 0.5f;
            bars.iconS = kSide;
            bars.iconR = s.look.muted[0];
            bars.iconG = s.look.muted[1];
            bars.iconB = s.look.muted[2];
            bars.iconA = 1.0f;
        }
        const UiText* shown = s.ent[id].try_get<UiText>();
        if (shown != nullptr && shown->len > 0) {
            const bool hint = field->len == 0 && field->hintLen > 0;
            ink.r = hint ? s.look.muted[0] : s.look.text[0];
            ink.g = hint ? s.look.muted[1] : s.look.text[1];
            ink.b = hint ? s.look.muted[2] : s.look.text[2];
            ink.a = 1.0f;
            ink.x = inset.x;
            ink.y = inset.y;
            ink.w = inset.w;
            ink.h = inset.h;
            ink.on = 1;
            UiBox ancestor{};
            if (uiClipBox(s, id, ancestor) && ancestor.w > 1.0f && ancestor.h > 1.0f) {
                const float x0 = inset.x > ancestor.x ? inset.x : ancestor.x;
                const float y0 = inset.y > ancestor.y ? inset.y : ancestor.y;
                const float x1 = inset.x + inset.w < ancestor.x + ancestor.w ? inset.x + inset.w : ancestor.x + ancestor.w;
                const float y1 = inset.y + inset.h < ancestor.y + ancestor.h ? inset.y + inset.h : ancestor.y + ancestor.h;
                if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) {
                    ink.on = 0;
                } else {
                    ink.cx = x0;
                    ink.cy = y0;
                    ink.cw = x1 - x0;
                    ink.ch = y1 - y0;
                    ink.clip = 1;
                }
            }
        }
        putChrome(s, id, bars, ink);
    }
}

} // namespace burnhope
