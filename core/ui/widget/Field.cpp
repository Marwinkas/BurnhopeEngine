#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

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
        paint->br = 0.86f;
        paint->bg = 0.34f;
        paint->bb = 0.28f;
        paint->borderW = 1.5f;
    } else {
        paint->br = 0.32f;
        paint->bg = 0.38f;
        paint->bb = 0.52f;
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

uint16_t fieldLabel(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return kUiNone;
    }
    const uint16_t label = s.labelOf[id];
    if (label >= s.count || s.parentOf[label] != id) {
        return kUiNone;
    }
    return label;
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

void fieldPlaceText(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (paint == nullptr || paint->role != static_cast<uint8_t>(UiRole::Field)) {
            continue;
        }
        const UiField* field = s.ent[id].try_get<UiField>();
        const uint16_t label = fieldLabel(s, id);
        if (field == nullptr || label == kUiNone) {
            continue;
        }
        auto* text = s.ent[label].try_get_mut<UiText>();
        auto* ink = s.ent[label].try_get_mut<UiPaint>();
        if (text == nullptr || ink == nullptr) {
            continue;
        }
        ink->token = static_cast<uint8_t>(UiToken::Custom);
        ink->a = 1.0f;
        if (field->len == 0 && field->hintLen > 0) {
            copyShown(*text, field->hint, field->hintLen);
            ink->r = s.look.muted[0];
            ink->g = s.look.muted[1];
            ink->b = s.look.muted[2];
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
            ink->r = field->text[0];
            ink->g = field->text[1];
            ink->b = field->text[2];
        } else {
            copyShown(*text, field->bytes, field->len);
            ink->r = field->text[0];
            ink->g = field->text[1];
            ink->b = field->text[2];
        }
        text->align = 0;
        text->wrap = 0;
        text->ellipsis = 0;
        text->scroll = field->scroll;
        if (s.box[id].w < 1.0f || s.box[id].h < 1.0f) {
            s.box[label] = {};
            continue;
        }
        UiBox slot = fieldInset(s, id, *field);
        slot.y = s.box[id].y;
        slot.h = s.box[id].h;
        s.box[label] = slot;
    }
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

void placeChromeText(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (paint == nullptr) {
            continue;
        }
        const auto role = static_cast<UiRole>(paint->role);
        if (role != UiRole::Button && role != UiRole::Close && role != UiRole::Check) {
            continue;
        }
        const uint16_t label = fieldLabel(s, id);
        const UiText* host = s.ent[id].try_get<UiText>();
        if (label == kUiNone || host == nullptr) {
            continue;
        }
        auto* text = s.ent[label].try_get_mut<UiText>();
        auto* ink = s.ent[label].try_get_mut<UiPaint>();
        if (text == nullptr || ink == nullptr) {
            continue;
        }
        copyShown(*text, host->bytes, host->len);
        ink->token = static_cast<uint8_t>(UiToken::Custom);
        ink->a = paint->disabled != 0 ? 0.45f : 1.0f;
        if (host->len == 0 || s.box[id].w < 1.0f || s.box[id].h < 1.0f) {
            s.box[label] = {};
            continue;
        }
        float tr = 0.93f;
        float tg = 0.94f;
        float tb = 0.96f;
        if (role == UiRole::Check) {
            tr = 0.93f;
            tg = 0.94f;
            tb = 0.96f;
        } else if (paint->a > 0.2f) {
            textInk(paint->r, paint->g, paint->b, id == s.hovered && paint->disabled == 0, tr, tg, tb);
        }
        ink->r = tr;
        ink->g = tg;
        ink->b = tb;
        text->scroll = 0;
        UiBox slot{};
        slot.y = s.box[id].y;
        slot.h = s.box[id].h;
        if (role == UiRole::Check) {
            slot.x = s.box[id].x + 32.0f;
            slot.w = s.box[id].w > 36.0f ? s.box[id].w - 36.0f : 1.0f;
        } else {
            slot.x = s.box[id].x;
            slot.w = s.box[id].w;
            if (paint->icon != 0) {
                slot.x += 22.0f;
                slot.w = slot.w > 22.0f ? slot.w - 22.0f : 1.0f;
            }
        }
        s.box[label] = slot;
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
    fieldInsert(*field, s.pasteBuf, s.pasteBufLen);
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
    s.visualDirty = true;
}

} // namespace burnhope
