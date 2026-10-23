#include "Str.hpp"
#include "ui/Icon.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Field.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void uiSetFieldText(UiState& s, uint16_t id, const char* text);

bool fieldAllows(uint8_t filter, uint32_t cp) {
    if (cp < 32 || cp == 127) {
        return false;
    }
    const bool digit = cp >= '0' && cp <= '9';
    const bool latin = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    const bool cyr = (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
    if (filter == static_cast<uint8_t>(UiFieldFilter::Digits)) {
        return digit;
    }
    if (filter == static_cast<uint8_t>(UiFieldFilter::Letters)) {
        return latin || cyr;
    }
    if (filter == static_cast<uint8_t>(UiFieldFilter::Email)) {
        return latin || digit || cyr || cp == '@' || cp == '.' || cp == '_' || cp == '+' || cp == '-';
    }
    if (filter == static_cast<uint8_t>(UiFieldFilter::Tel)) {
        return digit || cp == '+' || cp == '-' || cp == ' ' || cp == '(' || cp == ')';
    }
    if (filter == static_cast<uint8_t>(UiFieldFilter::Url)) {
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
    if (field.filter == static_cast<uint8_t>(UiFieldFilter::Email) && !emailOk(field)) {
        msg = "email name@host";
        return true;
    }
    if (field.filter == static_cast<uint8_t>(UiFieldFilter::Tel) && !telOk(field)) {
        msg = "tel needs 6 digits";
        return true;
    }
    if (field.filter == static_cast<uint8_t>(UiFieldFilter::Url) && !urlOk(field)) {
        msg = "url needs a dot";
        return true;
    }
    return false;
}

void uiFieldInit(
    UiState& s,
    uint16_t id,
    const char* text,
    const char* hint,
    uint8_t filter,
    uint8_t flags,
    uint8_t maxChars,
    float padL,
    float padR) {
    if (id == kUiNone || id >= s.count) {
        return;
    }
    UiField field{};
    if (text != nullptr) {
        uint8_t len = 0;
        while (text[len] != '\0' && len < 95) {
            field.bytes[len] = text[len];
            ++len;
        }
        field.bytes[len] = '\0';
        field.len = len;
        field.caret = len;
        field.anchor = len;
    }
    field.filter = filter;
    field.clear = (flags & 1) != 0 ? 1 : 0;
    field.required = (flags & 2) != 0 ? 1 : 0;
    field.readOnly = (flags & 4) != 0 ? 1 : 0;
    field.password = (flags & 8) != 0 ? 1 : 0;
    field.inForm = (flags & 16) != 0 ? 1 : 0;
    if (maxChars != 0) {
        field.maxChars = maxChars;
    }
    if (padL > 0.0f) {
        field.padL = padL;
    }
    if (padR > 0.0f) {
        field.padR = padR;
    }
    if (hint != nullptr && hint[0] != '\0') {
        uint8_t n = 0;
        while (hint[n] != '\0' && n < 39) {
            field.hint[n] = hint[n];
            ++n;
        }
        field.hint[n] = '\0';
        field.hintLen = n;
    }
    s.ent[id].set<UiField>(field);
    s.ent[id].add<UiFocusable>();
    s.ent[id].add<UiTextInput>();
    fieldSyncText(s, id);
    if ((flags & 16) != 0) {
        fieldMark(s, id);
    }
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
        flex->overflow = UiOverflow::Hidden;
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

void fieldSyncText(UiState& s, uint16_t id) {
    if (id >= s.count || !s.ent[id].is_alive()) {
        return;
    }
    const UiField* field = s.ent[id].try_get<UiField>();
    if (field == nullptr) {
        return;
    }
    UiText shown{};
    if (const UiText* live = s.ent[id].try_get<UiText>()) {
        shown = *live;
    }
    UiText* text = &shown;
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
    text->align = field->align == 1 ? UiTextAlign::Center : field->align == 2 ? UiTextAlign::Right : UiTextAlign::Left;
    text->wrap = field->multi;
    text->ellipsis = UiTextEllipsis::None;
    text->valign = UiTextValign::Center;
    text->scroll = field->scroll;
    text->ownRun = 1;
    s.ent[id].set<UiText>(shown);
    s.visualDirty = true;
}

UiField* editFieldOf(UiState& s) {
    if (s.editField == kUiNone || s.editField >= s.count) {
        return nullptr;
    }
    return s.ent[s.editField].try_get_mut<UiField>();
}

void cmdFieldCut(void* user, uint16_t) {
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

void cmdFieldCopy(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    const UiField* field = editFieldOf(s);
    if (field != nullptr) {
        fieldCopy(s, *field);
    }
}

void cmdFieldPaste(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr || field->readOnly != 0 || s.pasteBufLen == 0) {
        return;
    }
    fieldInsert(s, s.editField, s.pasteBuf, s.pasteBufLen);
    s.visualDirty = true;
}

void cmdFieldAll(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr) {
        return;
    }
    field->anchor = 0;
    field->caret = field->len;
    s.visualDirty = true;
}

void cmdFieldWipe(void* user, uint16_t) {
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

void uiAfterTextAdd(UiState& s, void (*fn)(UiState& s, uint16_t id)) {
    s.world.ensure<UiAfterText>().fn = fn;
}

void uiAfterText(UiState& s, uint16_t id) {
    const UiAfterText* hook = s.world.try_get<UiAfterText>();
    if (hook != nullptr && hook->fn != nullptr) {
        hook->fn(s, id);
    }
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
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.searchH;
    if (width < 0.0f) {
        flex.widthMode = UiSize::Percent;
        flex.width = 100.0f;
        flex.grow = 1.0f;
    } else {
        flex.widthMode = UiSize::Px;
        flex.width = width > 1.0f ? width : 160.0f;
    }
    const uint16_t id = uiPanel(s, parent, flex, paintField(s.look));
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    s.ent[id].add<UiTextInput>();
    UiField field{};
    copyText(field.hint, field.hintLen, hint != nullptr ? hint : "");
    s.ent[id].set<UiField>(field);
    fieldSyncText(s, id);
    if (onChange != nullptr) {
        s.ent[id].set<UiTextHook>({onChange, user});
    }
    return id;
}

void uiBindSearch(UiState& s, const char* name, UiTextChangeFn onChange, void* user) {
    const uint16_t id = uiFindName(s, name);
    if (id >= s.count || onChange == nullptr) {
        return;
    }
    s.ent[id].set<UiTextHook>({onChange, user});
}

uint16_t uiField(UiState& s, uint16_t parent, const char* text, uint8_t filter, bool clear) {
    UiFlex flex{};
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.fieldH;
    flex.shrink = 0.0f;
    const uint16_t id = uiPanel(s, parent, flex, paintField(s.look));
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    s.ent[id].add<UiTextInput>();
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
        s.grabX = in.pointer.x;
        s.relative = 0;
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

void uiSetFieldText(UiState& s, uint16_t id, const char* text) {
    if (id >= s.count) {
        return;
    }
    auto* field = s.ent[id].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    const uint8_t n = uiStrCopy(field->bytes, text);
    field->len = n;
    field->caret = n;
    field->anchor = n;
    field->scroll = 0.0f;
    fieldMark(s, id);
    fieldSyncText(s, id);
    s.visualDirty = true;
}

bool uiFieldDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id >= s.count) {
        return false;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    const UiField* field = s.ent[id].try_get<UiField>();
    if (paint == nullptr || field == nullptr) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    if (field->required != 0 && field->len == 0 && paint->borderW < 0.4f) {
        n = emitMark(
            dst, n, cap, box.x, box.y, box.w, box.h,
            s.look.accent[2][0], s.look.accent[2][1], s.look.accent[2][2],
            paint->a > 0.2f ? paint->a : 1.0f, paint->radius, 1.5f, kUiFlagStroke, clip);
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
                if (a >= b && rows[li].begin != rows[li].end) {
                    continue;
                }
                if (a >= b) {
                    continue;
                }
                const float x0 = origin + fieldOffset(s, *field, rows[li].begin, a);
                const float x1 = origin + fieldOffset(s, *field, rows[li].begin, b);
                const float y = inset.y + static_cast<float>(li) * lineH - field->scroll;
                if (x1 > x0 && y + lineH > box.y && y < box.y + box.h) {
                    n = emitRect(dst, n, cap, x0, y, x1 - x0, lineH, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.45f, 2.0f, clip);
                }
            }
        }
    } else if (sel0 != sel1 && field->len > 0) {
        const float x0 = origin + uiFieldWidth(s, *field, sel0);
        const float x1 = origin + uiFieldWidth(s, *field, sel1);
        const float left = x0 > inset.x ? x0 : inset.x;
        const float right = x1 < inset.x + inset.w ? x1 : inset.x + inset.w;
        if (right > left) {
            n = emitRect(dst, n, cap, left, box.y + 6.0f, right - left, box.h - 12.0f, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.45f, 2.0f, clip);
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
            n = emitRect(dst, n, cap, cx, cy, 1.5f, lineH, s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, 0.0f, clip);
        }
    } else if (id == s.focused && paint->disabled == 0 && field->scrub == 0) {
        const bool blink = (static_cast<int>(s.time * 1.6f) & 1) == 0;
        const float cx = origin + uiFieldWidth(s, *field, field->caret);
        if (blink && cx >= inset.x - 1.0f && cx <= inset.x + inset.w) {
            n = emitRect(dst, n, cap, cx, box.y + 7.0f, 1.5f, box.h - 14.0f, s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, 0.0f, clip);
        }
        if (s.imeLen > 0) {
            const float mark = s.look.advance * static_cast<float>(s.imeLen);
            n = emitRect(dst, n, cap, cx, box.y + box.h - 10.0f, mark, 2.0f,
                s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.9f, 0.0f, clip);
        }
    }
    const float clearW = field->clear != 0 && field->len > 0 ? 22.0f : 0.0f;
    if (clearW > 0.0f && n < cap) {
        constexpr float kSide = 12.0f;
        UiPrimitive& icon = dst[n++];
        icon = {};
        icon.posX = box.x + box.w - field->padR - kSide;
        icon.posY = box.y + (box.h - kSide) * 0.5f;
        icon.sizeX = kSide;
        icon.sizeY = kSide;
        icon.color[0] = s.look.muted[0];
        icon.color[1] = s.look.muted[1];
        icon.color[2] = s.look.muted[2];
        icon.color[3] = 1.0f;
        icon.texId = static_cast<uint32_t>(UiIcon::Dismiss);
        icon.flags = kUiFlagIcon;
    }
    const UiText* shown = s.ent[id].try_get<UiText>();
    if (shown != nullptr && shown->len > 0) {
        const bool hint = field->len == 0 && field->hintLen > 0;
        const float tr = hint ? s.look.muted[0] : s.look.text[0];
        const float tg = hint ? s.look.muted[1] : s.look.text[1];
        const float tb = hint ? s.look.muted[2] : s.look.text[2];
        UiBox limit = inset;
        bool outside = false;
        if (hasClip && ancestor.w > 1.0f && ancestor.h > 1.0f) {
            const float x0 = limit.x > ancestor.x ? limit.x : ancestor.x;
            const float y0 = limit.y > ancestor.y ? limit.y : ancestor.y;
            const float x1 = limit.x + limit.w < ancestor.x + ancestor.w ? limit.x + limit.w : ancestor.x + ancestor.w;
            const float y1 = limit.y + limit.h < ancestor.y + ancestor.h ? limit.y + limit.h : ancestor.y + ancestor.h;
            if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) {
                outside = true;
            } else {
                limit.x = x0;
                limit.y = y0;
                limit.w = x1 - x0;
                limit.h = y1 - y0;
            }
        }
        if (!outside) {
            const TextRun run = textRun(s, *shown, inset);
            if (run.lines > 0) {
                n = uiEmitRun(s, dst, n, cap, run, tr, tg, tb, 1.0f, &limit);
            }
        }
    }
    return false;
}

bool uiFieldInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    if (phase == kUiInCursor) {
        if (id >= s.count) {
            return false;
        }
        if (s.ent[id].try_get<UiField>() == nullptr) {
            return false;
        }
        const UiField* field = s.ent[id].try_get<UiField>();
        const float clearW = field != nullptr && field->clear != 0 && field->len > 0 && field->readOnly == 0 ? 22.0f : 0.0f;
        const bool onClear = clearW > 0.0f && in.pointer.x >= s.box[id].x + s.box[id].w - field->padR - clearW;
        cursor = onClear ? 6 : 5;
        return true;
    }
    if (phase == kUiInFocus) {
        if (id >= s.count) {
            return false;
        }
        if (s.ent[id].try_get<UiField>() == nullptr) {
            return false;
        }
        fieldHandlePointer(s, id, in, true, false);
        return true;
    }
    if (phase == kUiInDrag) {
        if (id >= s.count || s.ent[id].try_get<UiField>() == nullptr) {
            return false;
        }
        fieldHandlePointer(s, id, in, false, true);
        return true;
    }
    if (phase == kUiPhase4) {
        if ((in.pointer.pressed & kPointerRight) == 0 || id >= s.count) {
            return false;
        }
        if (s.ent[id].try_get<UiField>() == nullptr) {
            return false;
        }
        uiFocus(s, id);
        s.editField = id;
        uiNotifyVisualDirty(s);
        return true;
    }
    if (phase == kUiInKey) {
        return fieldHandleKey(s, id, in);
    }
    return false;
}

namespace {

void hostText(UiState& s, uint16_t id) {
    if (s.ent[id].try_get<UiText>() != nullptr) {
        return;
    }
    UiText host{};
    host.align = UiTextAlign::Left;
    host.ellipsis = UiTextEllipsis::None;
    host.valign = UiTextValign::Center;
    s.ent[id].set<UiText>(host);
}

void adornField(UiState& s, uint16_t id) {
    hostText(s, id);
}

void finishField(UiState& s, uint16_t id, const BinNode* spec, const char* text, const char* hint) {
    hostText(s, id);
    uint8_t flags = 0;
    uint8_t filter = 0;
    uint8_t maxChars = 0;
    float padL = 12.0f;
    float padR = 12.0f;
    if (spec != nullptr) {
        flags = static_cast<uint8_t>(spec->flags & (kBinClear | kBinRequired | kBinReadOnly | kBinPassword | kBinForm));
        filter = spec->filter;
        maxChars = spec->maxChars;
        if (spec->padL > 0.0f) {
            padL = spec->padL;
        }
        if (spec->padR > 0.0f) {
            padR = spec->padR;
        }
    }
    uiFieldInit(s, id, text, hint, filter, flags, maxChars, padL, padR);
}

uint16_t loadSearch(UiState& s, uint16_t parent, const BinNode& node, const char*, void*) {
    const float width = node.width > 1.0f ? node.width : 180.0f;
    const char* hint = s.importBag.hintLen > 0 ? s.importBag.hint : nullptr;
    return uiSearchField(s, parent, hint, nullptr, nullptr, width);
}

} // namespace

void uiFieldBin(UiState& s) {
    uiBinRole(s, kBinSlotField, "field", static_cast<uint8_t>(UiToken::Field), 0);
    uiBinRole(s, kBinSlotSearch, "search", 0, 0);
    uiRegisterSpawnHook(s, kBinSlotField, adornField);
    uiRegisterRoleFinish(s, kBinSlotField, finishField);
    uiRegisterCustomBinNode(s, kBinSlotSearch, loadSearch, nullptr);
}

} // namespace burnhope
