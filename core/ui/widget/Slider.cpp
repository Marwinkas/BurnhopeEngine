#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Slider.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

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

void dragSlider(UiState& s, uint16_t id, float x) {
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr) {
        return;
    }
    const UiBox& b = s.box[id];
    float t = b.w > 1.0f ? (x - b.x) / b.w : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    range->value = range->min + t * (range->max - range->min);
    if (const UiValueHook* hook = s.ent[id].try_get<UiValueHook>(); hook != nullptr && hook->fn != nullptr) {
        hook->fn(hook->user, range->value);
    }
    char note[32];
    std::snprintf(note, sizeof(note), "slider %.3f", range->value);
    uiTrace(s, note, id);
    s.visualDirty = true;
    for (uint8_t i = 0; i < s.valueN; ++i) {
        if (s.valueFn[i] != nullptr) {
            s.valueFn[i](s.valueUser[i], id, range->value);
        }
    }
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

uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange, void* user, float width) {
    UiFlex flex{};
    if (width > 1.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = width;
        flex.grow = 0.0f;
    } else {
        flex.widthMode = UiSize::Percent;
        flex.width = 100.0f;
    }
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.sliderH;
    flex.shrink = 0.0f;
    UiPaint body = paintToken(s.look, UiToken::Slider, s.look.radiusSm);
    body.a = 0.0f;
    body.borderW = 0.0f;
    body.shadow = 0.0f;
    const uint16_t id = uiPanel(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    if (value < minV) {
        value = minV;
    }
    if (value > maxV) {
        value = maxV;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, 0.0f});
    if (onChange != nullptr) {
        s.ent[id].set<UiValueHook>({onChange, user});
    }
    return id;
}

void uiBindSlider(UiState& s, const char* name, float value, UiValueChangeFn onChange, void* user) {
    const uint16_t id = uiFindName(s, name);
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    if (value < range->min) {
        value = range->min;
    }
    if (value > range->max) {
        value = range->max;
    }
    range->value = value;
    if (onChange != nullptr) {
        s.ent[id].set<UiValueHook>({onChange, user});
    }
}

void uiSliderSet(UiState& s, uint16_t id, float value) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    if (value < range->min) {
        value = range->min;
    }
    if (value > range->max) {
        value = range->max;
    }
    range->value = value;
}

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

bool uiSliderInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    (void)cursor;
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    if (phase == kUiInPressLate) {
        if (range->step > 0.0f) {
            return false;
        }
        s.capture = id;
        dragSlider(s, id, in.pointer.x);
        return true;
    }
    if (phase == kUiInDrag) {
        if (range->step > 0.0f) {
            const float x = s.relative != 0 ? s.grabX + in.pointer.dx : in.pointer.x;
            dragSpin(s, id, x);
            return true;
        }
        dragSlider(s, id, in.pointer.x);
        return true;
    }
    return false;
}

bool uiSliderDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr || range->step > 0.0f) {
        return false;
    }
    const UiBox& box = s.box[id];
    if (box.w < 8.0f || box.h < 8.0f) {
        return false;
    }
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    const float span = range->max - range->min;
    float t = span > 0.0001f ? (range->value - range->min) / span : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    const float inset = 2.0f;
    const float trackH = 4.0f;
    const float trackW = box.w - inset * 2.0f;
    const float trackY = box.y + (box.h - trackH) * 0.5f;
    n = emitRect(dst, n, cap, box.x + inset, trackY, trackW, trackH,
        s.look.muted[0], s.look.muted[1], s.look.muted[2], 0.7f, trackH * 0.5f, clip);
    n = emitRect(dst, n, cap, box.x + inset, trackY, trackW * t, trackH,
        s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, trackH * 0.5f, clip);
    const float thumb = box.h > 16.0f ? 14.0f : box.h - 4.0f;
    const float travel = trackW > thumb ? trackW - thumb : 0.0f;
    const float tx = box.x + inset + travel * t;
    const float ty = box.y + (box.h - thumb) * 0.5f;
    n = emitRect(dst, n, cap, tx, ty, thumb, thumb,
        s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, thumb * 0.5f, clip);
    return false;
}

} // namespace burnhope
