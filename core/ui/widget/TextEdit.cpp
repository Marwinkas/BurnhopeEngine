#include "ui/widget/Detail.hpp"

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
