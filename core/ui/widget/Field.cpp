#include "ui/widget/Detail.hpp"
#include "ui/widget/Field.hpp"

#include <cmath>
#include <cstring>

namespace burnhope {

bool fieldAllows(uint8_t filter, uint32_t cp) {
    if (cp < 32 || cp == 127) {
        return false;
    }
    const bool digit = cp >= '0' && cp <= '9';
    const bool latin = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    const bool cyr = (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
    if (filter == 1) {
        return digit;
    }
    if (filter == 2) {
        return latin || cyr;
    }
    if (filter == 3) {
        return latin || digit || cyr || cp == '@' || cp == '.' || cp == '_' || cp == '+' || cp == '-';
    }
    if (filter == 4) {
        return digit || cp == '+' || cp == '-' || cp == ' ' || cp == '(' || cp == ')';
    }
    if (filter == 5) {
        return latin || digit || cp == ':' || cp == '/' || cp == '.' || cp == '?' || cp == '&' || cp == '=' || cp == '%'
            || cp == '-' || cp == '_' || cp == '~';
    }
    return true;
}

bool emailOk(const UiField& field) {
    int at = -1;
    for (uint8_t i = 0; i < field.len; ++i) {
        if (field.bytes[i] == ' ') {
            return false;
        }
        if (field.bytes[i] == '@') {
            if (at >= 0 || i == 0) {
                return false;
            }
            at = i;
        }
    }
    if (at < 1 || at + 2 >= field.len) {
        return false;
    }
    bool dot = false;
    for (uint8_t i = static_cast<uint8_t>(at + 1); i < field.len; ++i) {
        if (field.bytes[i] == '.') {
            dot = i + 1 < field.len;
        }
    }
    return dot;
}

bool urlOk(const UiField& field) {
    bool colon = false;
    bool dot = false;
    for (uint8_t i = 0; i < field.len; ++i) {
        if (field.bytes[i] == ' ') {
            return false;
        }
        if (field.bytes[i] == ':') {
            colon = true;
        }
        if (field.bytes[i] == '.') {
            dot = true;
        }
    }
    return colon || dot;
}

bool telOk(const UiField& field) {
    uint8_t digits = 0;
    for (uint8_t i = 0; i < field.len; ++i) {
        if (field.bytes[i] >= '0' && field.bytes[i] <= '9') {
            ++digits;
        }
    }
    return digits >= 6;
}

bool fieldProblem(const UiField& field, const char*& msg) {
    msg = "";
    if (field.required != 0 && field.len == 0) {
        msg = "required";
        return true;
    }
    if (field.len == 0) {
        return false;
    }
    if (field.filter == 3 && !emailOk(field)) {
        msg = "email name@host";
        return true;
    }
    if (field.filter == 4 && !telOk(field)) {
        msg = "tel needs 6 digits";
        return true;
    }
    if (field.filter == 5 && !urlOk(field)) {
        msg = "url needs a dot";
        return true;
    }
    return false;
}

void fieldMark(UiState& s, uint16_t id) {
    const UiField* field = s.ent[id].try_get<UiField>();
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (field == nullptr || paint == nullptr) {
        return;
    }
    const char* msg = "";
    if (fieldProblem(*field, msg)) {
        paint->br = s.look.accent[2][0];
        paint->bg = s.look.accent[2][1];
        paint->bb = s.look.accent[2][2];
        paint->borderW = 1.5f;
    } else {
        paint->br = s.look.muted[0];
        paint->bg = s.look.muted[1];
        paint->bb = s.look.muted[2];
        paint->borderW = 1.0f;
    }
    s.visualDirty = true;
}

UiBox fieldInset(const UiState& s, uint16_t id, const UiField& field) {
    const UiBox& box = s.box[id];
    const float clearW = field.clear != 0 && field.len > 0 ? 22.0f : 0.0f;
    UiBox inset = box;
    inset.x += field.padL;
    inset.w = box.w - field.padL - field.padR - clearW;
    if (inset.w < 1.0f) {
        inset.w = 1.0f;
    }
    return inset;
}

void copyShown(UiText& text, const char* src, uint8_t n) {
    if (n > 95) {
        n = 95;
    }
    for (uint8_t i = 0; i < n; ++i) {
        text.bytes[i] = src[i];
    }
    text.bytes[n] = '\0';
    text.len = n;
}

void uiFieldMulti(UiState& s, uint16_t id) {
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return;
    }
    field->multi = 1;
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->overflow = 1;
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

void fieldSyncText(UiState& s, uint16_t id) {
    if (id >= s.count || !s.ent[id].is_alive()) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr || paint->role != static_cast<uint8_t>(UiRole::Field)) {
        return;
    }
    const UiField* field = s.ent[id].try_get<UiField>();
    auto* text = s.ent[id].try_get_mut<UiText>();
    if (field == nullptr || text == nullptr) {
        return;
    }
    if (field->len == 0 && field->hintLen > 0) {
        copyShown(*text, field->hint, field->hintLen);
    } else if (field->password != 0) {
        char mask[96]{};
        uint8_t stars = 0;
        uint8_t bi = 0;
        while (bi < field->len && stars < 95) {
            uint8_t step = 1;
            uiReadUtf8(field->bytes, field->len, bi, step);
            if (step == 0) {
                break;
            }
            mask[stars++] = '*';
            bi = static_cast<uint8_t>(bi + step);
        }
        copyShown(*text, mask, stars);
    } else {
        copyShown(*text, field->bytes, field->len);
    }
    text->align = 0;
    text->wrap = field->multi;
    text->ellipsis = 0;
    text->valign = 1;
    text->scroll = field->scroll;
    s.visualDirty = true;
}

void textInk(float r, float g, float b, bool hot, float& tr, float& tg, float& tb) {
    const float lum = 0.30f * r + 0.59f * g + 0.11f * b;
    if (lum > 0.72f) {
        tr = 0.10f;
        tg = 0.11f;
        tb = 0.14f;
    } else {
        tr = 0.93f;
        tg = 0.94f;
        tb = 0.96f;
    }
    if (hot) {
        tr = tr + 0.08f > 1.0f ? 1.0f : tr + 0.08f;
        tg = tg + 0.08f > 1.0f ? 1.0f : tg + 0.08f;
        tb = tb + 0.08f > 1.0f ? 1.0f : tb + 0.08f;
    }
}

UiField* editFieldOf(UiState& s) {
    if (s.editField == kUiNone || s.editField >= s.count) {
        return nullptr;
    }
    return s.ent[s.editField].try_get_mut<UiField>();
}

void cmdFieldCut(void* user) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr || field->readOnly != 0) {
        return;
    }
    fieldCopy(s, *field);
    fieldDropSel(*field);
    fieldSyncText(s, s.editField);
    s.visualDirty = true;
}

void cmdFieldCopy(void* user) {
    auto& s = *static_cast<UiState*>(user);
    const UiField* field = editFieldOf(s);
    if (field != nullptr) {
        fieldCopy(s, *field);
    }
}

void cmdFieldPaste(void* user) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr || field->readOnly != 0 || s.pasteBufLen == 0) {
        return;
    }
    fieldInsert(s, s.editField, s.pasteBuf, s.pasteBufLen);
    s.visualDirty = true;
}

void cmdFieldAll(void* user) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr) {
        return;
    }
    field->anchor = 0;
    field->caret = field->len;
    s.visualDirty = true;
}

void cmdFieldWipe(void* user) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr || field->readOnly != 0) {
        return;
    }
    field->len = 0;
    field->caret = 0;
    field->anchor = 0;
    field->scroll = 0.0f;
    field->bytes[0] = '\0';
    fieldSyncText(s, s.editField);
    s.visualDirty = true;
}

void uiFieldNotify(UiState& s, uint16_t id) {
    const UiTextHook* hook = id < s.count ? s.ent[id].try_get<UiTextHook>() : nullptr;
    const UiField* field = hook != nullptr ? s.ent[id].try_get<UiField>() : nullptr;
    if (hook != nullptr && hook->fn != nullptr && field != nullptr) {
        hook->fn(hook->user, field->bytes);
    }
}

uint16_t uiSearchField(UiState& s, uint16_t parent, const char* hint, UiTextChangeFn onChange, void* user, float width) {
    UiFlex flex{};
    flex.shrink = 0.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 26.0f;
    if (width < 0.0f) {
        flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex.width = 100.0f;
        flex.grow = 1.0f;
    } else {
        flex.widthMode = static_cast<uint8_t>(UiSize::Px);
        flex.width = width > 1.0f ? width : 160.0f;
    }
    const uint16_t id = spawn(s, parent, flex, paintToken(s.look, UiRole::Field, UiToken::Field, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    UiField field{};
    copyText(field.hint, field.hintLen, hint != nullptr ? hint : "");
    s.ent[id].set<UiField>(field);
    fieldSyncText(s, id);
    if (onChange != nullptr) {
        s.ent[id].set<UiTextHook>({onChange, user});
    }
    return id;
}

uint16_t uiField(UiState& s, uint16_t parent, const char* text, uint8_t filter, bool clear) {
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    flex.width = 100.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 36.0f;
    flex.shrink = 0.0f;
    const uint16_t id = spawn(s, parent, flex, paintToken(s.look, UiRole::Field, UiToken::Field, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    UiField field{};
    const uint8_t n = uiStrCopy(field.bytes, text);
    field.len = n;
    field.caret = n;
    field.anchor = n;
    field.filter = filter;
    field.clear = clear ? 1 : 0;
    s.ent[id].set<UiField>(field);
    fieldSyncText(s, id);
    if (s.fieldMenu != kUiNone) {
        s.contextOf[id] = s.fieldMenu;
    }
    return id;
}

void uiBindField(UiState& s, uint16_t id, const char* text, uint8_t filter, bool clear) {
    if (id >= s.count) {
        return;
    }
    UiField field{};
    const uint8_t n = uiStrCopy(field.bytes, text);
    field.len = n;
    field.caret = n;
    field.anchor = n;
    field.filter = filter;
    field.clear = clear ? 1 : 0;
    s.ent[id].set<UiField>(field);
    fieldSyncText(s, id);
    if (s.fieldMenu != kUiNone) {
        s.contextOf[id] = s.fieldMenu;
    }
    s.visualDirty = true;
}

void uiBindFieldExtra(UiState& s, uint16_t id, const char* placeholder, uint8_t maxChars, bool required, bool readOnly, bool password) {
    if (id >= s.count) {
        return;
    }
    auto* field = s.ent[id].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    field->maxChars = maxChars;
    field->required = required ? 1 : 0;
    field->readOnly = readOnly ? 1 : 0;
    field->password = password ? 1 : 0;
    field->hintLen = 0;
    if (placeholder != nullptr) {
        while (placeholder[field->hintLen] != '\0' && field->hintLen < 39) {
            field->hint[field->hintLen] = placeholder[field->hintLen];
            ++field->hintLen;
        }
    }
    field->hint[field->hintLen] = '\0';
    fieldSyncText(s, id);
    s.visualDirty = true;
}

namespace {

uint8_t caretFromPointer(const UiState& s, uint16_t id, const UiField& field, float x, float y) {
    const float local = x - (s.box[id].x + field.padL) + (field.multi == 0 ? field.scroll : 0.0f);
    const float localY = y - s.box[id].y + (field.multi != 0 ? field.scroll : 0.0f);
    const float viewW = s.box[id].w - field.padL - field.padR;
    return field.multi != 0 ? fieldCaretAt2(s, field, local, localY, viewW) : fieldCaretAt(s, field, local);
}

float fieldView(const UiState& s, uint16_t id, const UiField& field) {
    const float clearW = field.clear != 0 && field.len > 0 && field.readOnly == 0 ? 22.0f : 0.0f;
    return s.box[id].w - field.padL - field.padR - clearW;
}

void moveCaret(UiField& field, int dir, bool extend) {
    if (!extend && field.anchor != field.caret) {
        const uint8_t edge = dir < 0
            ? (field.anchor < field.caret ? field.anchor : field.caret)
            : (field.anchor > field.caret ? field.anchor : field.caret);
        field.caret = edge;
        field.anchor = edge;
        return;
    }
    if (dir < 0) {
        uint8_t prev = field.caret > 0 ? static_cast<uint8_t>(field.caret - 1) : 0;
        while (prev > 0 && (static_cast<unsigned char>(field.bytes[prev]) & 0xC0) == 0x80) {
            --prev;
        }
        field.caret = prev;
    } else if (field.caret < field.len) {
        uint8_t step = 1;
        uiReadUtf8(field.bytes, field.len, field.caret, step);
        field.caret = static_cast<uint8_t>(field.caret + (step == 0 ? 1 : step));
    }
    if (!extend) {
        field.anchor = field.caret;
    }
}

} // namespace

bool fieldHandlePointer(UiState& s, uint16_t id, const InputFrame& in, bool press, bool down) {
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return false;
    }
    if (press) {
        s.editField = id;
        const float clearW = field->clear != 0 && field->len > 0 && field->readOnly == 0 ? 22.0f : 0.0f;
        if (clearW > 0.0f && in.pointer.x >= s.box[id].x + s.box[id].w - field->padR - clearW) {
            field->len = 0;
            field->caret = 0;
            field->anchor = 0;
            field->scroll = 0.0f;
            field->bytes[0] = '\0';
            fieldSyncText(s, id);
            s.visualDirty = true;
            return true;
        }
        if (in.clicks >= 3) {
            field->anchor = 0;
            field->caret = field->len;
            s.visualDirty = true;
            return true;
        }
        if (in.clicks >= 2) {
            fieldSelectWord(*field, caretFromPointer(s, id, *field, in.pointer.x, in.pointer.y));
            s.visualDirty = true;
            return true;
        }
        field->caret = caretFromPointer(s, id, *field, in.pointer.x, in.pointer.y);
        field->anchor = field->caret;
        s.capture = id;
        fieldReveal(s, *field, fieldView(s, id, *field));
        s.visualDirty = true;
        return true;
    }
    if (down && s.capture == id) {
        field->caret = caretFromPointer(s, id, *field, in.pointer.x, in.pointer.y);
        fieldReveal(s, *field, fieldView(s, id, *field));
        s.visualDirty = true;
        return true;
    }
    return false;
}

bool fieldHandleKey(UiState& s, uint16_t id, const InputFrame& in) {
    auto* field = id < s.count ? s.ent[id].try_get_mut<UiField>() : nullptr;
    if (field == nullptr) {
        return false;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr || paint->disabled != 0) {
        return true;
    }
    if (in.enter != 0) {
        if (field->multi != 0 && field->len < 95 && field->readOnly == 0) {
            fieldRemember(s, id, *field);
            fieldInsert(s, id, "\n", 1);
            s.visualDirty = true;
            return true;
        }
        return false;
    }
    bool edited = false;
    if (in.undo != 0) {
        edited = fieldUndo(s);
    } else if (in.redo != 0) {
        edited = fieldRedo(s);
    } else {
        const bool typing = in.selectAll != 0 || in.cut != 0 || (in.pasteOn != 0 && in.pasteLen > 0) || (in.ctrl == 0 && in.textLen > 0)
            || in.backspace != 0 || in.del != 0;
        if (typing) {
            fieldRemember(s, id, *field);
        }
        if (in.selectAll != 0) {
            field->anchor = 0;
            field->caret = field->len;
            edited = true;
        }
        if (in.copy != 0 || in.cut != 0) {
            fieldCopy(s, *field);
            if (in.cut != 0) {
                fieldDropSel(*field);
                edited = true;
            }
        }
        if (in.pasteOn != 0 && in.pasteLen > 0) {
            fieldInsert(s, id, in.paste, in.pasteLen);
            edited = true;
        }
        if (in.ctrl == 0 && in.textLen > 0) {
            fieldInsert(s, id, in.text, in.textLen);
            edited = true;
        }
        if (in.backspace != 0) {
            fieldEraseBack(s, id);
            edited = true;
        }
        if (in.del != 0) {
            fieldEraseForward(s, id);
            edited = true;
        }
        if (in.left != 0) {
            moveCaret(*field, -1, in.shift != 0);
            edited = true;
        }
        if (in.right != 0) {
            moveCaret(*field, 1, in.shift != 0);
            edited = true;
        }
        if (field->multi != 0 && (in.up != 0 || in.down != 0)) {
            const float viewW = s.box[id].w - field->padL - field->padR;
            FieldRow rows[16]{};
            const int rn = fieldRows(s, *field, viewW, rows, 16);
            const float lineH = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
            int line = 0;
            for (int li = 0; li < rn; ++li) {
                if (field->caret >= rows[li].begin && field->caret <= rows[li].end) {
                    line = li;
                }
            }
            const float x = fieldOffset(s, *field, rows[line].begin, field->caret);
            line += in.up != 0 ? -1 : 1;
            if (line < 0) {
                line = 0;
            }
            if (line >= rn) {
                line = rn - 1;
            }
            field->caret = fieldCaretAt2(s, *field, x, static_cast<float>(line) * lineH + 1.0f, viewW);
            if (in.shift == 0) {
                field->anchor = field->caret;
            }
            edited = true;
        }
    }
    if (edited) {
        fieldReveal(s, *field, fieldView(s, id, *field));
        fieldMark(s, id);
        if (const UiEdit* edit = s.ent[id].try_get<UiEdit>(); edit != nullptr && edit->fn != nullptr) {
            edit->fn(edit->user, id);
        }
        fieldSyncText(s, id);
        uiFieldNotify(s, id);
        s.visualDirty = true;
    }
    return true;
}

} // namespace burnhope
