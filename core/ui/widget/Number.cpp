#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Number.hpp"

#include <charconv>
#include <cstdio>

namespace burnhope {

void uiRangeSet(UiState& s, uint16_t id, float value) {
    if (id >= s.count) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr || range->value == value) {
        return;
    }
    range->value = value;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(range->value));
    uiSetFieldText(s, id, text);
    uiNotifyVisualDirty(s);
}

bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV) {
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    value = range->value;
    minV = range->min;
    maxV = range->max;
    return true;
}

void dragSpin(UiState& s, uint16_t id, float x) {
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr) {
        return;
    }
    const float delta = x - s.grabX;
    if (delta > -3.0f && delta < 3.0f) {
        return;
    }
    const float step = range->step > 0.0f ? range->step : 0.01f;
    range->value += (delta / 8.0f) * step;
    if (range->value < range->min) {
        range->value = range->min;
    }
    if (range->value > range->max) {
        range->value = range->max;
    }
    s.grabX = x;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(range->value));
    uiText(s, id, text);
    s.visualDirty = true;
    for (uint8_t i = 0; i < s.valueN; ++i) {
        if (s.valueFn[i] != nullptr) {
            s.valueFn[i](s.valueUser[i], id, range->value);
        }
    }
}

namespace {

void publish(UiState& s, uint16_t id, float value) {
    if (const UiValueHook* hook = s.ent[id].try_get<UiValueHook>(); hook != nullptr && hook->fn != nullptr) {
        hook->fn(hook->user, value);
    }
}

void writeValue(UiState& s, uint16_t id, float value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
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

uint16_t uiNumber(UiState& s, uint16_t parent, float value, float minV, float maxV, float step, UiValueChangeFn onChange, void* user) {
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
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
    const uint16_t id = uiField(s, parent, text, 0, false);
    if (id == kUiNone) {
        return id;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, step, 1});
    if (auto* field = s.ent[id].try_get_mut<UiField>()) {
        field->align = 0;
    }
    fieldSyncText(s, id);
    if (onChange != nullptr) {
        s.ent[id].set<UiValueHook>({onChange, user});
    }
    return id;
}

void uiNumberFill(UiState& s, uint16_t id, bool showFillProgress) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    range->fill = showFillProgress ? 1 : 0;
}

uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step) {
    UiFlex flex{};
    flex.heightMode = UiSize::Px;
    flex.height = 30.0f;
    flex.shrink = 0.0f;
    flex.align = UiAlign::Center;
    UiPaint body = paintToken(s.look, UiToken::Button, s.look.radiusSm);
    body.shadow = 4.0f;
    body.borderW = 2.0f;
    body.br = s.look.text[0];
    body.bg = s.look.text[1];
    body.bb = s.look.text[2];
    const uint16_t id = uiPanel(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    if (value < minV) {
        value = minV;
    }
    if (value > maxV) {
        value = maxV;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, step > 0.0f ? step : 0.01f});
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(value));
    uiText(s, id, text);
    return id;
}

void uiNumberWatch(UiState& s) {
    uiAfterTextAdd(s, syncRangeText);
}

bool uiNumberInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t&) {
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
        s.editField = id;
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
        const uint16_t edit = s.editField;
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
            s.editField = kUiNone;
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

bool uiNumberDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id >= s.count) {
        return false;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (paint == nullptr || range == nullptr || range->fill == 0 || range->step <= 0.0f) {
        return false;
    }
    if (range->max <= range->min + 0.0001f || range->max - range->min >= 1.0e8f) {
        return false;
    }
    float span = (range->value - range->min) / (range->max - range->min);
    if (span < 0.0f) {
        span = 0.0f;
    }
    if (span > 1.0f) {
        span = 1.0f;
    }
    if (span <= 0.001f) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    n = emitRect(dst, n, cap, box.x + 1.0f, box.y + 1.0f, (box.w - 2.0f) * span, box.h - 2.0f,
        s.look.muted[0], s.look.muted[1], s.look.muted[2], 0.85f, paint->radius, clip);
    return false;
}

} // namespace burnhope
