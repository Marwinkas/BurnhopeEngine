#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

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

uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV) {
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    flex.width = 100.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 18.0f;
    flex.shrink = 0.0f;
    const uint16_t id = spawn(s, parent, flex, paint(UiRole::Slider, 0.18f, 0.20f, 0.26f, 8.0f));
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
    return id;
}

uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step) {
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 30.0f;
    flex.shrink = 0.0f;
    flex.align = 1;
    const uint16_t id = spawn(s, parent, flex, paint(UiRole::Button, 0.18f, 0.20f, 0.26f, 6.0f));
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
