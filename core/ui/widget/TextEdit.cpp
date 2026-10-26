#include "ui/widget/Field.hpp"
#include "ui/State.hpp"

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
        // Typed text so far = bytes before the caret + accepted run; the tail after the caret is the "rest".
        char head[192]{};
        uint8_t headLen = 0;
        for (uint8_t k = 0; k < field.caret; ++k) {
            head[headLen++] = field.bytes[k];
        }
        for (uint8_t k = 0; k < accept; ++k) {
            head[headLen++] = tmp[k];
        }
        const bool ok = fieldAllowsAt(field.filter, cp, head, headLen, field.bytes + field.caret, static_cast<uint8_t>(field.len - field.caret));
        if (room && ok && static_cast<uint8_t>(field.len + accept + step) < 95) {
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

UiFieldUndo* undoOf(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return nullptr;
    }
    if (!s.ent[id].has<UiFieldUndo>()) {
        s.ent[id].set<UiFieldUndo>({});
    }
    return s.ent[id].try_get_mut<UiFieldUndo>();
}

flecs::entity snapEnt(UiState& s, uint64_t id) {
    return flecs::entity(s.world, id);
}

flecs::entity snapAt(UiState& s, const UiFieldUndo& undo, uint16_t index) {
    flecs::entity e = snapEnt(s, undo.head);
    for (uint16_t i = 0; i < index && e.is_alive(); ++i) {
        const UiFieldSnap* snap = e.try_get<UiFieldSnap>();
        if (snap == nullptr) {
            return {};
        }
        e = snapEnt(s, snap->next);
    }
    return e;
}

void dropRedo(UiState& s, UiFieldUndo& undo) {
    if (undo.at >= undo.n) {
        return;
    }
    flecs::entity e = snapAt(s, undo, undo.at);
    if (undo.at == 0) {
        undo.head = 0;
    } else {
        flecs::entity prev = snapAt(s, undo, static_cast<uint16_t>(undo.at - 1));
        if (auto* snap = prev.is_alive() ? prev.try_get_mut<UiFieldSnap>() : nullptr) {
            snap->next = 0;
        }
    }
    while (e.is_alive()) {
        flecs::entity next{};
        if (const UiFieldSnap* snap = e.try_get<UiFieldSnap>()) {
            next = snapEnt(s, snap->next);
        }
        e.destruct();
        e = next;
    }
    undo.n = undo.at;
}

void fieldRemember(UiState& s, uint16_t id, const UiField& field) {
    UiFieldUndo* undo = undoOf(s, id);
    if (undo == nullptr) {
        return;
    }
    dropRedo(s, *undo);
    if (undo->at > 0) {
        flecs::entity prev = snapAt(s, *undo, static_cast<uint16_t>(undo->at - 1));
        const UiFieldSnap* snap = prev.is_alive() ? prev.try_get<UiFieldSnap>() : nullptr;
        if (snap != nullptr && snap->len == field.len && std::memcmp(snap->bytes, field.bytes, field.len) == 0) {
            return;
        }
    }
    UiFieldSnap snap{};
    snap.len = field.len;
    snap.caret = field.caret;
    snap.anchor = field.anchor;
    if (field.len > 0) {
        std::memcpy(snap.bytes, field.bytes, field.len);
    }
    flecs::entity e = s.world.entity();
    if (undo->at == 0) {
        undo->head = e.id();
    } else {
        flecs::entity prev = snapAt(s, *undo, static_cast<uint16_t>(undo->at - 1));
        snap.prev = prev.id();
        if (auto* older = prev.is_alive() ? prev.try_get_mut<UiFieldSnap>() : nullptr) {
            older->next = e.id();
        }
    }
    e.set<UiFieldSnap>(snap);
    ++undo->at;
    undo->n = undo->at;
}

void swapField(UiField& field, UiFieldSnap& snap) {
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

bool fieldUndo(UiState& s) {
    const uint16_t id = s.focused;
    UiFieldUndo* undo = undoOf(s, id);
    auto* field = undo != nullptr ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (undo == nullptr || field == nullptr || undo->at == 0) {
        return false;
    }
    --undo->at;
    flecs::entity e = snapAt(s, *undo, undo->at);
    auto* snap = e.is_alive() ? e.try_get_mut<UiFieldSnap>() : nullptr;
    if (snap == nullptr) {
        return false;
    }
    swapField(*field, *snap);
    fieldSyncText(s, id);
    uiAfterText(s, id);
    s.visualDirty = true;
    return true;
}

bool fieldRedo(UiState& s) {
    const uint16_t id = s.focused;
    UiFieldUndo* undo = undoOf(s, id);
    auto* field = undo != nullptr ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (undo == nullptr || field == nullptr || undo->at >= undo->n) {
        return false;
    }
    flecs::entity e = snapAt(s, *undo, undo->at);
    auto* snap = e.is_alive() ? e.try_get_mut<UiFieldSnap>() : nullptr;
    if (snap == nullptr) {
        return false;
    }
    swapField(*field, *snap);
    ++undo->at;
    fieldSyncText(s, id);
    uiAfterText(s, id);
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
    UiClipOut& clip = s.world.ensure<UiClipOut>();
    uint8_t n = 0;
    while (a + n < b && n < 95) {
        clip.bytes[n] = field.bytes[a + n];
        ++n;
    }
    clip.bytes[n] = '\0';
    clip.len = n;
    clip.put = 1;
}

} // namespace burnhope
