#include "Str.hpp"
#include "ui/Bar.hpp"
#include "ui/Icon.hpp"
#include "ui/Primitive.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Field.hpp"
#include "ui/State.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

float uiFieldWidth(const UiState& s, const UiField& field, uint8_t until) {
    if (field.password == 0) {
        return uiMeasure(s, field.bytes, field.len, until);
    }
    uint8_t count = 0;
    uint8_t i = 0;
    const uint8_t end = until < field.len ? until : field.len;
    while (i < end) {
        uint8_t step = 1;
        uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        ++count;
        i = static_cast<uint8_t>(i + step);
    }
    return static_cast<float>(count) * uiAdvance(s, static_cast<uint32_t>('*'));
}


void uiSetFieldText(UiState& s, uint16_t id, const char* text);
void uiFieldAlign(UiState& s, uint16_t id, uint8_t align);

bool fieldProblem(const UiField& field, const char*& msg) {
    msg = "";
    if (field.required != 0 && field.len == 0) {
        msg = "required";
        return true;
    }
    if (field.len == 0) {
        return false;
    }
    return !fieldCheck(field.filter, field.bytes, field.len, msg);
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
        text->len = static_cast<uint8_t>(strCopyCap(text->bytes, sizeof(text->bytes), field->hint));
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
        text->len = static_cast<uint8_t>(strCopyCap(text->bytes, sizeof(text->bytes), mask));
    } else {
        text->len = static_cast<uint8_t>(strCopyCap(text->bytes, sizeof(text->bytes), field->bytes));
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

void uiSetField(UiState& s, uint16_t id) {
    uiMarkOnly<UiNamedField>(s, id);
}

uint16_t uiField(const UiState& s) {
    return uiMarked<UiNamedField>(s);
}

void uiSetFieldMenu(UiState& s, uint16_t id) {
    uiMarkOnly<UiFieldMenuMark>(s, id);
}

uint16_t uiFieldMenu(const UiState& s) {
    return uiMarked<UiFieldMenuMark>(s);
}

void uiFieldAcceptPaste(UiState& s, uint16_t id, const char* text) {
    UiFieldPaste paste{};
    paste.len = static_cast<uint8_t>(strCopyCap(paste.bytes, sizeof(paste.bytes), text));
    uint16_t dest = uiFieldEdit(s);
    if (dest >= s.count) {
        dest = id;
    }
    if (dest >= s.count) {
        return;
    }
    uiMarkOnly<UiFieldPaste>(s, dest);
    s.ent[dest].set<UiFieldPaste>(paste);
}

void uiFieldSetEdit(UiState& s, uint16_t id) {
    if (id >= s.count) {
        uiMarkOnly<UiFieldCaret>(s, kUiNone);
        return;
    }
    const uint16_t holder = uiMarked<UiFieldPaste>(s);
    UiFieldPaste carried{};
    const bool have = holder != kUiNone && holder != id;
    if (have) {
        carried = *s.ent[holder].try_get<UiFieldPaste>();
        s.ent[holder].remove<UiFieldPaste>();
    }
    uiMarkOnly<UiFieldCaret>(s, id);
    if (have) {
        s.ent[id].set<UiFieldPaste>(carried);
    }
}

uint16_t uiFieldEdit(const UiState& s) {
    return uiMarked<UiFieldCaret>(s);
}

UiField* editFieldOf(UiState& s) {
    const uint16_t edit = uiFieldEdit(s);
    if (edit == kUiNone || edit >= s.count) {
        return nullptr;
    }
    return s.ent[edit].try_get_mut<UiField>();
}

void cmdFieldCut(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    UiField* field = editFieldOf(s);
    if (field == nullptr || field->readOnly != 0) {
        return;
    }
    fieldCopy(s, *field);
    fieldDropSel(*field);
    fieldSyncText(s, uiFieldEdit(s));
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
    const uint16_t edit = uiFieldEdit(s);
    UiField* field = edit < s.count ? s.ent[edit].try_get_mut<UiField>() : nullptr;
    const UiFieldPaste* paste = edit < s.count ? s.ent[edit].try_get<UiFieldPaste>() : nullptr;
    if (field == nullptr || field->readOnly != 0 || paste == nullptr || paste->len == 0) {
        return;
    }
    fieldInsert(s, edit, paste->bytes, paste->len);
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
    fieldSyncText(s, uiFieldEdit(s));
    s.visualDirty = true;
}

void uiAfterTextAdd(UiState& s, uint16_t id, void (*fn)(UiState& s, uint16_t id)) {
    if (id >= s.count || fn == nullptr) {
        return;
    }
    s.ent[id].set<UiAfterText>({fn});
}

void uiAfterText(UiState& s, uint16_t id) {
    const UiAfterText* hook = id < s.count ? s.ent[id].try_get<UiAfterText>() : nullptr;
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
    if (uiFieldMenu(s) != kUiNone) {
        s.contextOf[id] = uiFieldMenu(s);
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
    if (uiFieldMenu(s) != kUiNone) {
        s.contextOf[id] = uiFieldMenu(s);
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
        uiFieldSetEdit(s, id);
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

void uiFieldAlign(UiState& s, uint16_t id, uint8_t align) {
    if (id >= s.count) {
        return;
    }
    if (auto* field = s.ent[id].try_get_mut<UiField>()) {
        field->align = align;
    }
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

bool uiFieldInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note) {
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
        note.cursor = onClear ? 6 : 5;
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
        uiFieldSetEdit(s, id);
        uiNotifyVisualDirty(s);
        return true;
    }
    if (phase == kUiInKey) {
        if (id < s.count && s.ent[id].try_get<UiField>() != nullptr && in.pasteLen > 0) {
            uiFieldAcceptPaste(s, id, in.paste);
        }
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

void finishField(UiState& s, uint16_t id, const BinTail* spec, const char* text, const char* hint) {
    hostText(s, id);
    uint8_t flags = 0;
    uint8_t filter = 0;
    uint8_t maxChars = 0;
    float padL = 12.0f;
    float padR = 12.0f;
    if (const UiFlex* flex = s.ent[id].try_get<UiFlex>()) {
        if (flex->padL > 0.0f) {
            padL = flex->padL;
        }
        if (flex->padR > 0.0f) {
            padR = flex->padR;
        }
    }
    if (spec != nullptr) {
        constexpr uint8_t kClear = 1 << 0;
        constexpr uint8_t kRequired = 1 << 1;
        constexpr uint8_t kReadOnly = 1 << 2;
        constexpr uint8_t kPassword = 1 << 3;
        constexpr uint8_t kForm = 1 << 4;
        filter = spec->raw[0];
        flags = static_cast<uint8_t>(spec->raw[1] & (kClear | kRequired | kReadOnly | kPassword | kForm));
        maxChars = spec->raw[4];
    }
    uiFieldInit(s, id, text, hint, filter, flags, maxChars, padL, padR);
}

} // namespace

void uiFieldBin(UiState& s) {
    uiPlaceAdd(s, fieldPlace);
    uiBinRole(s, kBinSlotField, "field", static_cast<uint8_t>(UiToken::Field), 0);
    uiRegisterSpawnHook(s, kBinSlotField, adornField);
    uiRegisterRoleFinish(s, kBinSlotField, finishField);
}

} // namespace burnhope
