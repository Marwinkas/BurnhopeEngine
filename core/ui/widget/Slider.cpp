#include "ui/widget/Detail.hpp"
#include "ui/widget/Slider.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

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
        flex.widthMode = static_cast<uint8_t>(UiSize::Px);
        flex.width = width;
        flex.grow = 0.0f;
    } else {
        flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex.width = 100.0f;
    }
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 18.0f;
    flex.shrink = 0.0f;
    UiPaint body = paintToken(s.look, UiRole::Slider, UiToken::Slider, s.look.radiusSm);
    body.shadow = 3.0f;
    body.borderW = 2.0f;
    body.br = s.look.text[0];
    body.bg = s.look.text[1];
    body.bb = s.look.text[2];
    const uint16_t id = spawn(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
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

uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step) {
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 30.0f;
    flex.shrink = 0.0f;
    flex.align = 1;
    UiPaint body = paintToken(s.look, UiRole::Button, UiToken::Button, s.look.radiusSm);
    body.shadow = 4.0f;
    body.borderW = 2.0f;
    body.br = s.look.text[0];
    body.bg = s.look.text[1];
    body.bb = s.look.text[2];
    const uint16_t id = spawn(s, parent, flex, body);
    if (value < minV) {
        value = minV;
    }
    if (value > maxV) {
        value = maxV;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, step > 0.0f ? step : 0.01f});
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(value));
    addText(s, id, text);
    return id;
}

} // namespace burnhope
