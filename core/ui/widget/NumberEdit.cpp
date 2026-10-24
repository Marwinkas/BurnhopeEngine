#include "ui/widget/Field.hpp"
#include "ui/widget/Number.hpp"

#include <charconv>

namespace burnhope {
namespace {

void publish(UiState& s, uint16_t id, float value) {
    if (const UiValueHook* hook = s.ent[id].try_get<UiValueHook>(); hook != nullptr && hook->fn != nullptr) {
        hook->fn(hook->user, value);
    }
}

void writeValue(UiState& s, uint16_t id, float value) {
    char text[32]{};
    uiWriteNumber(text, sizeof(text), value);
    uiSetFieldText(s, id, text);
    publish(s, id, value);
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

bool numberField(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    return range != nullptr && range->step > 0.0f && s.ent[id].try_get<UiField>() != nullptr;
}

} // namespace

uint16_t uiNumber(UiState& s, uint16_t parent, float value, float minV, float maxV, float step, UiValueChangeFn onChange, void* user, float width, float height, float shrink) {
    if (value < minV) {
        value = minV;
    }
    if (value > maxV) {
        value = maxV;
    }
    if (step <= 0.0f) {
        step = 0.01f;
    }
    char text[32]{};
    uiWriteNumber(text, sizeof(text), value);
    const uint16_t id = uiField(s, parent, text, 0, false);
    if (id == kUiNone) {
        return id;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, step, 1});
    if (width > 0.0f || height > 0.0f) {
        uiFlexLimit(s, id, width, height, shrink);
    }
    if (auto* field = s.ent[id].try_get_mut<UiField>()) {
        field->align = 0;
    }
    fieldSyncText(s, id);
    if (onChange != nullptr) {
        s.ent[id].set<UiValueHook>({onChange, user});
    }
    uiAfterTextAdd(s, id, syncRangeText);
    return id;
}

void uiRangeSet(UiState& s, uint16_t id, float value) {
    if (id >= s.count) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr || range->value == value) {
        return;
    }
    range->value = value;
    if (s.ent[id].try_get<UiField>() != nullptr) {
        char text[32]{};
        uiWriteNumber(text, sizeof(text), range->value);
        uiSetFieldText(s, id, text);
    }
    uiNotifyVisualDirty(s);
}

void uiNumberWatch(UiState& s) {
    uiNumberBind(s);
}

bool uiNumberInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote&) {
    if (phase == kUiInPress) {
        if (!numberField(s, id)) {
            return false;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr) {
            return false;
        }
        s.capture = id;
        s.grabX = in.pointer.x;
        uiFieldSetEdit(s, id);
        field->scrub = 1;
        return true;
    }
    if (phase == kUiInDrag) {
        if (id >= s.count) {
            return false;
        }
        auto* range = s.ent[id].try_get_mut<UiRange>();
        if (range == nullptr || range->step <= 0.0f) {
            return false;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr) {
            const float x = s.relative != 0 ? s.grabX + in.pointer.dx : in.pointer.x;
            dragSpin(s, id, x);
            return true;
        }
        float dx = 0.0f;
        if (s.relative != 0) {
            dx = in.pointer.dx;
        } else {
            dx = in.pointer.x - s.grabX;
            if (dx > -3.0f && dx < 3.0f) {
                return true;
            }
            s.relative = 1;
            s.grabX = in.pointer.x;
        }
        if (dx == 0.0f) {
            return true;
        }
        field->scrub = 2;
        if (s.focused == id) {
            uiFocus(s, kUiNone);
        }
        range->value += (dx / 6.0f) * range->step;
        if (range->value < range->min) {
            range->value = range->min;
        }
        if (range->value > range->max) {
            range->value = range->max;
        }
        writeValue(s, id, range->value);
        s.visualDirty = true;
        return true;
    }
    if (phase == kUiInRelease) {
        const uint16_t edit = uiFieldEdit(s);
        auto* field = edit < s.count ? s.ent[edit].try_get_mut<UiField>() : nullptr;
        if (field == nullptr || field->scrub == 0) {
            return false;
        }
        const uint8_t scrub = field->scrub;
        field->scrub = 0;
        if (scrub == 1) {
            uiFocus(s, edit);
            fieldHandlePointer(s, edit, in, true, false);
        } else {
            uiFieldSetEdit(s, kUiNone);
        }
        return true;
    }
    if (phase == kUiInKey && in.enter != 0 && numberField(s, id)) {
        auto* range = s.ent[id].try_get_mut<UiRange>();
        const UiField* field = s.ent[id].try_get<UiField>();
        if (range == nullptr || field == nullptr || field->readOnly != 0) {
            return false;
        }
        float value = range->value;
        const auto parsed = std::from_chars(field->bytes, field->bytes + field->len, value);
        if (parsed.ec == std::errc{}) {
            if (value < range->min) {
                value = range->min;
            }
            if (value > range->max) {
                value = range->max;
            }
            range->value = value;
            writeValue(s, id, value);
        }
        return true;
    }
    return false;
}

} // namespace burnhope
