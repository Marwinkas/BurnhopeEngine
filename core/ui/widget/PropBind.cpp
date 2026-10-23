#include "ui/widget/PropBind.hpp"

#include "ui/widget/Button.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Slider.hpp"

#include <cstdio>

namespace burnhope {

void UiPropBinder::clear() {
    nFloats = 0;
    nBools = 0;
    nBits = 0;
    nSwatches = 0;
}

void UiPropBinder::watch(uint16_t id, float* dst, float fallback) {
    if (dst == nullptr) {
        return;
    }
    *dst = fallback;
    if (nFloats < kFloats) {
        floats[nFloats++] = {id, dst, fallback};
    }
}

uint16_t UiPropBinder::slideStep(UiState& s, uint16_t panel, const char* label, float* dst, float def, float minV, float maxV, float step) {
    if (dst != nullptr) {
        *dst = def;
    }
    const uint16_t id = uiPropNumber(s, panel, label, def, minV, maxV, step);
    if (dst != nullptr && nFloats < kFloats) {
        floats[nFloats++] = {id, dst, def};
    }
    return id;
}

uint16_t UiPropBinder::slide(UiState& s, uint16_t panel, const char* label, float* dst, float def, float minV, float maxV) {
    const float span = maxV - minV;
    float step = 0.01f;
    if (span > 40.0f) {
        step = 1.0f;
    } else if (span > 4.0f) {
        step = 0.1f;
    }
    return slideStep(s, panel, label, dst, def, minV, maxV, step);
}

void UiPropBinder::slides(UiState& s, uint16_t panel, const UiSlideSpec* spec, uint16_t count) {
    if (spec == nullptr) {
        return;
    }
    for (uint16_t i = 0; i < count; ++i) {
        slide(s, panel, spec[i].label, spec[i].dst, spec[i].def, spec[i].minV, spec[i].maxV);
    }
}

uint16_t UiPropBinder::check(UiState& s, uint16_t panel, const char* label, uint8_t* dst, bool def) {
    if (dst != nullptr) {
        *dst = def ? 1 : 0;
    }
    const uint16_t id = uiPropCheck(s, panel, label, 0, 0, def);
    if (dst != nullptr && nBools < kBools) {
        bools[nBools++] = {id, dst, static_cast<uint8_t>(def ? 1 : 0)};
    }
    return id;
}

uint16_t UiPropBinder::check(UiState& s, uint16_t panel, const char* label, bool* dst, bool def) {
    if (dst != nullptr) {
        *dst = def;
    }
    const uint16_t id = uiPropCheck(s, panel, label, 0, 0, def);
    if (dst != nullptr && nBits < kBools) {
        bits[nBits++] = {id, dst, static_cast<uint8_t>(def ? 1 : 0)};
    }
    return id;
}

uint16_t UiPropBinder::swatch(UiState& s, uint16_t panel, const char* label, float* r, float* g, float* b, float r0, float g0, float b0) {
    if (r != nullptr) {
        *r = r0;
    }
    if (g != nullptr) {
        *g = g0;
    }
    if (b != nullptr) {
        *b = b0;
    }
    const uint16_t id = uiPropSwatch(s, panel, label, r0, g0, b0);
    if (nSwatches < kSwatches) {
        swatches[nSwatches++] = {id, r, g, b};
    }
    return id;
}

void UiPropBinder::sync(const UiState& s) const {
    for (uint16_t i = 0; i < nFloats; ++i) {
        float value = floats[i].fallback;
        float minV = 0.0f;
        float maxV = 1.0f;
        if (floats[i].dst != nullptr && uiRange(s, floats[i].id, value, minV, maxV)) {
            *floats[i].dst = value;
        }
    }
    for (uint16_t i = 0; i < nBools; ++i) {
        if (bools[i].dst == nullptr) {
            continue;
        }
        const int on = uiCheckOn(s, bools[i].id);
        *bools[i].dst = on < 0 ? bools[i].fallback : static_cast<uint8_t>(on != 0);
    }
    for (uint16_t i = 0; i < nBits; ++i) {
        if (bits[i].dst == nullptr) {
            continue;
        }
        const int on = uiCheckOn(s, bits[i].id);
        *bits[i].dst = on < 0 ? bits[i].fallback != 0 : on != 0;
    }
}

void UiPropBinder::syncSwatches(UiState& s) const {
    UiColorPickerComponent& picker = uiColor(s);
    if (picker.book == nullptr || picker.colorPresent == 0 || picker.colorAnchor == kUiNone) {
        return;
    }
    for (uint16_t i = 0; i < nSwatches; ++i) {
        if (swatches[i].id != picker.colorAnchor) {
            continue;
        }
        if (swatches[i].r != nullptr) {
            *swatches[i].r = picker.book->r;
        }
        if (swatches[i].g != nullptr) {
            *swatches[i].g = picker.book->g;
        }
        if (swatches[i].b != nullptr) {
            *swatches[i].b = picker.book->b;
        }
        uiPaintRgb(s, swatches[i].id, picker.book->r, picker.book->g, picker.book->b);
    }
}

void UiPropBinder::write(UiState& s, float* dst, float value) const {
    if (dst == nullptr) {
        return;
    }
    const uint16_t id = idOf(dst);
    if (id != kUiNone && id < s.count && (s.capture == id || s.focused == id)) {
        return;
    }
    *dst = value;
    if (id == kUiNone || id >= s.count) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr || range->value == value) {
        return;
    }
    range->value = value;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
    uiSetFieldText(s, id, text);
    uiNotifyVisualDirty(s);
}

uint16_t UiPropBinder::idOf(const float* dst) const {
    for (uint16_t i = 0; i < nFloats; ++i) {
        if (floats[i].dst == dst) {
            return floats[i].id;
        }
    }
    return kUiNone;
}

uint16_t UiPropBinder::idOf(const uint8_t* dst) const {
    for (uint16_t i = 0; i < nBools; ++i) {
        if (bools[i].dst == dst) {
            return bools[i].id;
        }
    }
    return kUiNone;
}

} // namespace burnhope
