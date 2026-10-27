#include "ui/Bar.hpp"
#include "ui/Icon.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Field.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

#include <cstdint>

namespace burnhope {

namespace {

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
    if (nf == 0) {
        return;
    }
    using K = UiStyleKey;
    using T = UiStyleTone;
    const UiRgb errorInk = uiStyleTint(s, "input_field", T::Error);
    const UiRgb fillInk = uiStyleTint(s, "input_field", T::Fill);
    const UiRgb textInk = uiStyleTint(s, "input_field", T::Text);
    const UiRgb hintInk = uiStyleTint(s, "input_field", T::Hint);
    const UiRgb clearInk = uiStyleTint(s, "input_field", T::Clear);
    const float lineH = s.look.lineH();
    const float caretW = uiStyle(s, "input_field", K::CaretW);
    const float caretInset = uiStyle(s, "input_field", K::CaretInset);
    const float selectionAlpha = uiStyle(s, "input_field", K::SelectionAlpha);
    const float selectionInset = uiStyle(s, "input_field", K::SelectionInset);
    const float selectionRadius = uiStyle(s, "input_field", K::SelectionRadius);
    const float imeOffset = uiStyle(s, "input_field", K::ImeOffset);
    const float imeH = uiStyle(s, "input_field", K::ImeH);
    const float imeAlpha = uiStyle(s, "input_field", K::ImeAlpha);
    const float blinkRate = uiStyle(s, "input_field", K::BlinkRate);
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
        if (field->required != 0 && field->len == 0 && paint->borderW < kUiBorderMinW) {
            uiBarPush(s, bars, box.x, box.y, box.w, box.h,
                errorInk.r, errorInk.g, errorInk.b,
                paint->a > 0.2f ? paint->a : 1.0f, paint->radius, kUiFlagStroke, uiStyle(s, "input_field", UiStyleKey::ErrorBorder));
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
                        uiBarPush(s, bars, x0, y, x1 - x0, lineH, fillInk.r, fillInk.g, fillInk.b, selectionAlpha, selectionRadius);
                    }
                }
            }
        } else if (sel0 != sel1 && field->len > 0) {
            const float x0 = origin + uiFieldWidth(s, *field, sel0);
            const float x1 = origin + uiFieldWidth(s, *field, sel1);
            const float left = x0 > inset.x ? x0 : inset.x;
            const float right = x1 < inset.x + inset.w ? x1 : inset.x + inset.w;
            if (right > left) {
                uiBarPush(s, bars, left, box.y + selectionInset, right - left, box.h - 2.0f * selectionInset, fillInk.r, fillInk.g, fillInk.b, selectionAlpha, selectionRadius);
            }
        }
        if (id == s.focused && paint->disabled == 0 && field->multi != 0) {
            FieldRow rows[16]{};
            const int rn = fieldRows(s, *field, inset.w, rows, 16);
            int line = 0;
            for (int li = 0; li < rn; ++li) {
                if (field->caret >= rows[li].begin && field->caret <= rows[li].end) {
                    line = li;
                }
            }
            const float cx = origin + fieldOffset(s, *field, rows[line].begin, field->caret);
            const float cy = inset.y + static_cast<float>(line) * lineH - field->scroll;
            if (cy + lineH > box.y && cy < box.y + box.h) {
                uiBarPush(s, bars, cx, cy, caretW, lineH, textInk.r, textInk.g, textInk.b, 1.0f, 0.0f);
            }
        } else if (id == s.focused && paint->disabled == 0 && field->scrub == 0) {
            const bool blink = (static_cast<int>(s.time * blinkRate) & 1) == 0;
            const float cx = origin + uiFieldWidth(s, *field, field->caret);
            if (blink && cx >= inset.x - 1.0f && cx <= inset.x + inset.w) {
                uiBarPush(s, bars, cx, box.y + caretInset, caretW, box.h - 2.0f * caretInset, textInk.r, textInk.g, textInk.b, 1.0f, 0.0f);
            }
            const UiIme* ime = s.ent[id].try_get<UiIme>();
            if (ime != nullptr && ime->len > 0) {
                const float mark = s.look.advance * static_cast<float>(ime->len);
                uiBarPush(s, bars, cx, box.y + box.h - imeOffset, mark, imeH, fillInk.r, fillInk.g, fillInk.b, imeAlpha, 0.0f);
            }
        }
        if (field->clear != 0 && field->len > 0) {
            const float kSide = uiStyle(s, "input_field", UiStyleKey::ClearIcon);
            bars.icon = static_cast<uint8_t>(UiIcon::Dismiss);
            bars.iconX = box.x + box.w - field->padR - kSide;
            bars.iconY = box.y + (box.h - kSide) * 0.5f;
            bars.iconS = kSide;
            bars.iconR = clearInk.r;
            bars.iconG = clearInk.g;
            bars.iconB = clearInk.b;
            bars.iconA = 1.0f;
        }
        const UiText* shown = s.ent[id].try_get<UiText>();
        if (shown != nullptr && shown->len > 0) {
            const bool hint = field->len == 0 && field->hintLen > 0;
            const UiRgb& shade = hint ? hintInk : textInk;
            ink.r = shade.r;
            ink.g = shade.g;
            ink.b = shade.b;
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
