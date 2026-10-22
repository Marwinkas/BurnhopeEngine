#include "ui/widget/Detail.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Slider.hpp"

#include <charconv>
#include <cstring>

namespace burnhope {

uint8_t fieldChars(const UiField& field) {
    uint8_t count = 0;
    uint8_t i = 0;
    while (i < field.len) {
        uint8_t step = 1;
        uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        ++count;
        i = static_cast<uint8_t>(i + step);
    }
    return count;
}

void fieldSelectWord(UiField& field, uint8_t at) {
    auto word = [](uint32_t cp) {
        return fieldAllows(2, cp) || (cp >= '0' && cp <= '9') || cp == '_';
    };
    uint8_t start = at;
    while (start > 0) {
        uint8_t prev = static_cast<uint8_t>(start - 1);
        while (prev > 0 && (static_cast<unsigned char>(field.bytes[prev]) & 0xC0) == 0x80) {
            --prev;
        }
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(field.bytes, field.len, prev, step);
        if (!word(cp)) {
            break;
        }
        start = prev;
    }
    uint8_t end = at;
    while (end < field.len) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(field.bytes, field.len, end, step);
        if (step == 0 || !word(cp)) {
            break;
        }
        end = static_cast<uint8_t>(end + step);
    }
    if (end <= start) {
        field.anchor = at;
        field.caret = at;
        return;
    }
    field.anchor = start;
    field.caret = end;
}

void fieldDropSel(UiField& field) {
    uint8_t a = field.anchor < field.caret ? field.anchor : field.caret;
    uint8_t b = field.anchor < field.caret ? field.caret : field.anchor;
    if (a == b) {
        return;
    }
    const uint8_t tail = static_cast<uint8_t>(field.len - b);
    for (uint8_t i = 0; i < tail; ++i) {
        field.bytes[a + i] = field.bytes[b + i];
    }
    field.len = static_cast<uint8_t>(a + tail);
    field.bytes[field.len] = '\0';
    field.caret = a;
    field.anchor = a;
}

void fieldInsert(UiState& s, uint16_t id, const char* src, uint8_t n) {
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return;
    }
    fieldInsert(*field, src, n);
    fieldSyncText(s, id);
}

void fieldInsert(UiField& field, const char* src, uint8_t n) {
    if (field.readOnly != 0 || src == nullptr || n == 0) {
        return;
    }
    fieldDropSel(field);
    uint8_t accept = 0;
    uint8_t added = 0;
    const uint8_t have = fieldChars(field);
    char tmp[96]{};
    for (uint8_t i = 0; i < n && accept < 95;) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(src, n, i, step);
        if (step == 0) {
            break;
        }
        const bool room = field.maxChars == 0 || static_cast<uint8_t>(have + added) < field.maxChars;
        if (room && fieldAllows(field.filter, cp) && static_cast<uint8_t>(field.len + accept + step) < 95) {
            ++added;
            for (uint8_t k = 0; k < step; ++k) {
                tmp[accept++] = src[i + k];
            }
        }
        i = static_cast<uint8_t>(i + step);
    }
    if (accept == 0 || static_cast<uint8_t>(field.len + accept) >= 95) {
        return;
    }
    for (uint8_t i = field.len; i > field.caret; --i) {
        field.bytes[i + accept - 1] = field.bytes[i - 1];
    }
    for (uint8_t i = 0; i < accept; ++i) {
        field.bytes[field.caret + i] = tmp[i];
    }
    field.len = static_cast<uint8_t>(field.len + accept);
    field.caret = static_cast<uint8_t>(field.caret + accept);
    field.anchor = field.caret;
    field.bytes[field.len] = '\0';
}

void fieldRemember(UiState& s, uint16_t id, const UiField& field) {
    UiFieldUndo& undo = uiUndo(s);
    if (undo.at < undo.n) {
        undo.n = undo.at;
    }
    if (undo.at > 0) {
        const auto& prev = undo.snap[undo.at - 1];
        if (prev.id == id && prev.len == field.len && std::memcmp(prev.bytes, field.bytes, field.len) == 0) {
            return;
        }
    }
    if (undo.at >= 8) {
        for (uint8_t i = 1; i < 8; ++i) {
            undo.snap[i - 1] = undo.snap[i];
        }
        undo.at = 7;
        undo.n = 7;
    }
    auto& snap = undo.snap[undo.at];
    snap = {};
    snap.id = id;
    snap.len = field.len;
    snap.caret = field.caret;
    snap.anchor = field.anchor;
    std::memcpy(snap.bytes, field.bytes, field.len);
    ++undo.at;
    undo.n = undo.at;
}

void swapField(UiField& field, UiFieldUndo::Snap& snap) {
    UiField copy = field;
    field.len = snap.len;
    field.caret = snap.caret;
    field.anchor = snap.anchor;
    std::memcpy(field.bytes, snap.bytes, snap.len);
    field.bytes[field.len] = '\0';
    snap.len = copy.len;
    snap.caret = copy.caret;
    snap.anchor = copy.anchor;
    std::memcpy(snap.bytes, copy.bytes, copy.len);
    snap.bytes[snap.len] = '\0';
}

void syncRangeText(UiState& s, uint16_t id) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    const UiField* field = id < s.count ? s.ent[id].try_get<UiField>() : nullptr;
    if (range == nullptr || field == nullptr || range->step <= 0.0f) {
        return;
    }
    float value = range->value;
    const auto parsed = std::from_chars(field->bytes, field->bytes + field->len, value);
    if (parsed.ec != std::errc{}) {
        return;
    }
    if (range->max - range->min < 1.0e8f) {
        if (value < range->min) {
            value = range->min;
        }
        if (value > range->max) {
            value = range->max;
        }
    }
    range->value = value;
}

bool fieldUndo(UiState& s) {
    UiFieldUndo& undo = uiUndo(s);
    if (undo.at == 0) {
        return false;
    }
    const uint16_t id = undo.snap[undo.at - 1].id < s.count ? undo.snap[undo.at - 1].id : s.focused;
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return false;
    }
    --undo.at;
    undo.snap[undo.at].id = id;
    swapField(*field, undo.snap[undo.at]);
    fieldSyncText(s, id);
    syncRangeText(s, id);
    s.visualDirty = true;
    return true;
}

bool fieldRedo(UiState& s) {
    UiFieldUndo& undo = uiUndo(s);
    if (undo.at >= undo.n) {
        return false;
    }
    const uint16_t id = undo.snap[undo.at].id < s.count ? undo.snap[undo.at].id : s.focused;
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return false;
    }
    swapField(*field, undo.snap[undo.at]);
    undo.snap[undo.at].id = id;
    ++undo.at;
    fieldSyncText(s, id);
    syncRangeText(s, id);
    s.visualDirty = true;
    return true;
}

void fieldEraseBack(UiState& s, uint16_t id) {
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return;
    }
    fieldEraseBack(*field);
    fieldSyncText(s, id);
}

void fieldEraseBack(UiField& field) {
    if (field.readOnly != 0) {
        return;
    }
    if (field.anchor != field.caret) {
        fieldDropSel(field);
        return;
    }
    if (field.caret == 0 || field.len == 0) {
        return;
    }
    uint8_t prev = static_cast<uint8_t>(field.caret - 1);
    while (prev > 0 && (static_cast<unsigned char>(field.bytes[prev]) & 0xC0) == 0x80) {
        --prev;
    }
    const uint8_t tail = static_cast<uint8_t>(field.len - field.caret);
    for (uint8_t i = 0; i < tail; ++i) {
        field.bytes[prev + i] = field.bytes[field.caret + i];
    }
    field.len = static_cast<uint8_t>(prev + tail);
    field.bytes[field.len] = '\0';
    field.caret = prev;
    field.anchor = prev;
}

void fieldEraseForward(UiState& s, uint16_t id) {
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return;
    }
    fieldEraseForward(*field);
    fieldSyncText(s, id);
}

void fieldEraseForward(UiField& field) {
    if (field.readOnly != 0) {
        return;
    }
    if (field.anchor != field.caret) {
        fieldDropSel(field);
        return;
    }
    if (field.caret >= field.len) {
        return;
    }
    uint8_t step = 1;
    uiReadUtf8(field.bytes, field.len, field.caret, step);
    if (step == 0) {
        return;
    }
    const uint8_t next = static_cast<uint8_t>(field.caret + step);
    const uint8_t tail = static_cast<uint8_t>(field.len - next);
    for (uint8_t i = 0; i < tail; ++i) {
        field.bytes[field.caret + i] = field.bytes[next + i];
    }
    field.len = static_cast<uint8_t>(field.caret + tail);
    field.bytes[field.len] = '\0';
}

void fieldReveal(const UiState& s, UiField& field, float view) {
    if (field.multi != 0) {
        FieldRow rows[16]{};
        const int n = fieldRows(s, field, view, rows, 16);
        const float lineH = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
        int line = 0;
        for (int i = 0; i < n; ++i) {
            const bool on = field.caret >= rows[i].begin && (field.caret < rows[i].end || field.caret == rows[i].end);
            if (on) {
                line = i;
            }
        }
        const float y = static_cast<float>(line) * lineH;
        const float viewH = s.focused < s.count && s.box[s.focused].h > lineH ? s.box[s.focused].h : lineH * 2.0f;
        if (y - field.scroll > viewH - lineH) {
            field.scroll = y - viewH + lineH;
        }
        if (y < field.scroll) {
            field.scroll = y;
        }
        if (field.scroll < 0.0f) {
            field.scroll = 0.0f;
        }
        return;
    }
    const float x = uiFieldWidth(s, field, field.caret);
    if (view < 8.0f) {
        view = 8.0f;
    }
    if (x - field.scroll > view) {
        field.scroll = x - view;
    }
    if (x < field.scroll) {
        field.scroll = x;
    }
    if (field.scroll < 0.0f) {
        field.scroll = 0.0f;
    }
}

uint8_t fieldCaretAt(const UiState& s, const UiField& field, float local) {
    uint8_t best = 0;
    float prev = 0.0f;
    uint8_t i = 0;
    while (i < field.len) {
        uint8_t step = 1;
        uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        const uint8_t next = static_cast<uint8_t>(i + step);
        const float at = uiFieldWidth(s, field, next);
        if (local < (prev + at) * 0.5f) {
            return i;
        }
        best = next;
        prev = at;
        i = next;
    }
    return best;
}

namespace {

struct FieldGlyph {
    uint8_t at = 0;
    uint8_t n = 1;
    uint8_t kind = 0;
    float adv = 0;
};

int fieldGlyphs(const UiState& s, const UiField& field, FieldGlyph* out, int cap) {
    int n = 0;
    uint8_t i = 0;
    while (i < field.len && n < cap) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        FieldGlyph g{};
        g.at = i;
        g.n = step;
        g.kind = cp == '\n' ? 1 : (cp == ' ' ? 2 : 0);
        g.adv = g.kind == 1 ? 0.0f : uiAdvance(s, field.password != 0 ? static_cast<uint32_t>('*') : cp);
        out[n++] = g;
        i = static_cast<uint8_t>(i + step);
    }
    return n;
}

} // namespace

int fieldRows(const UiState& s, const UiField& field, float viewW, FieldRow* rows, int cap) {
    if (rows == nullptr || cap <= 0) {
        return 0;
    }
    if (field.multi == 0 || viewW < 8.0f) {
        rows[0] = {0, field.len, uiFieldWidth(s, field, field.len)};
        return 1;
    }
    FieldGlyph glyph[96]{};
    const int n = fieldGlyphs(s, field, glyph, 96);
    float ps[97]{};
    for (int k = 0; k < n; ++k) {
        ps[k + 1] = ps[k] + glyph[k].adv;
    }
    int at = 0;
    int lines = 0;
    while (at < n && lines < cap) {
        if (glyph[at].kind == 1) {
            rows[lines++] = {glyph[at].at, glyph[at].at, 0.0f};
            ++at;
            continue;
        }
        uint8_t kind[96]{};
        for (int k = 0; k < n; ++k) {
            kind[k] = glyph[k].kind;
        }
        const UiWrapCut cut = uiWrapCut(ps, kind, n, at, viewW, 0);
        const int end = cut.end;
        const int next = cut.next;
        const uint8_t begin = glyph[at].at;
        const uint8_t stop = static_cast<uint8_t>(glyph[end - 1].at + glyph[end - 1].n);
        rows[lines++] = {begin, stop, ps[end] - ps[at]};
        at = next;
    }
    if (lines == 0) {
        rows[lines++] = {0, 0, 0.0f};
    }
    if (n > 0 && glyph[n - 1].kind == 1 && lines < cap) {
        rows[lines++] = {field.len, field.len, 0.0f};
    }
    return lines;
}

float fieldOffset(const UiState& s, const UiField& field, uint8_t from, uint8_t to) {
    float w = 0.0f;
    uint8_t i = from;
    const uint8_t end = to < field.len ? to : field.len;
    while (i < end) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0 || cp == '\n') {
            break;
        }
        w += uiAdvance(s, field.password != 0 ? static_cast<uint32_t>('*') : cp);
        i = static_cast<uint8_t>(i + step);
    }
    return w;
}

uint8_t fieldCaretAt2(const UiState& s, const UiField& field, float localX, float localY, float viewW) {
    if (field.multi == 0) {
        return fieldCaretAt(s, field, localX);
    }
    FieldRow rows[16]{};
    const int n = fieldRows(s, field, viewW, rows, 16);
    if (n <= 0) {
        return 0;
    }
    const float lineH = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
    int line = static_cast<int>(localY / lineH);
    if (line < 0) {
        line = 0;
    }
    if (line >= n) {
        line = n - 1;
    }
    const FieldRow& row = rows[line];
    float prev = 0.0f;
    uint8_t best = row.begin;
    uint8_t i = row.begin;
    const uint8_t end = row.end > field.len ? field.len : row.end;
    while (i < end) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        const uint8_t next = static_cast<uint8_t>(i + step);
        const float at = prev + uiAdvance(s, field.password != 0 ? static_cast<uint32_t>('*') : cp);
        if (localX < (prev + at) * 0.5f) {
            return i;
        }
        best = next;
        prev = at;
        i = next;
    }
    return best;
}

void fieldCopy(UiState& s, const UiField& field) {
    const uint8_t a = field.anchor < field.caret ? field.anchor : field.caret;
    const uint8_t b = field.anchor < field.caret ? field.caret : field.anchor;
    if (a == b) {
        return;
    }
    uint8_t n = 0;
    while (a + n < b && n < 95) {
        s.clip[n] = field.bytes[a + n];
        ++n;
    }
    s.clip[n] = '\0';
    s.clipLen = n;
    s.clipPut = 1;
}

} // namespace burnhope
