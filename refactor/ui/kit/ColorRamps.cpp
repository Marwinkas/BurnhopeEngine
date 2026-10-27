#include "Str.hpp"
#include "color/Color.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/kit/ColorInternal.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Slider.hpp"

#include <algorithm>
#include <cmath>

namespace burnhope {

UiPaint colorRampPart(const UiLook& look, UiToken token, UiRamp ramp, float radius) {
    UiPaint paint = paintToken(look, token, radius);
    paint.ramp = ramp;
    return paint;
}

void setRange(UiState& s, uint16_t id, float value) {
    if (id == kUiNone) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range != nullptr) {
        range->value = value;
    }
}

void writeHex(char* dst, uint8_t& len, float r, float g, float b, float a) {
    len = static_cast<uint8_t>(colorFormatHex(dst, 10, r, g, b, a, true));
}

void applyColor(UiState& s, UiColorPickerComponent& picker, bool fromHsv, bool publish) {
    if (fromHsv) {
        ColorLinear linear{};
        colorHsvToLinear(colorInk(picker).h, colorInk(picker).s, colorInk(picker).v, linear);
        colorInk(picker).r = linear.r;
        colorInk(picker).g = linear.g;
        colorInk(picker).b = linear.b;
    } else {
        colorLinearToHsv(colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).h, colorInk(picker).s, colorInk(picker).v);
    }
    setRange(s, picker.colorRgb[0], colorInk(picker).r);
    setRange(s, picker.colorRgb[1], colorInk(picker).g);
    setRange(s, picker.colorRgb[2], colorInk(picker).b);
    setRange(s, picker.colorHsv[0], colorInk(picker).h);
    setRange(s, picker.colorHsv[1], colorInk(picker).s);
    setRange(s, picker.colorHsv[2], colorInk(picker).v);
    for (uint16_t slot : {picker.colorRgb[0], picker.colorRgb[1], picker.colorRgb[2], picker.colorAlpha, picker.colorHsv[0], picker.colorHsv[1], picker.colorHsv[2]}) {
        if (slot == kUiNone || slot >= s.count) {
            continue;
        }
        auto* ramp = s.ent[slot].try_get_mut<UiPaint>();
        if (ramp != nullptr) {
            ramp->r = colorInk(picker).r;
            ramp->g = colorInk(picker).g;
            ramp->b = colorInk(picker).b;
        }
    }
    auto write = [&](uint16_t id, bool keepCaret) {
        if (id == kUiNone) {
            return;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr) {
            return;
        }
        if (keepCaret && s.focused == id) {
            return;
        }
        writeHex(field->bytes, field->len, colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).a);
        field->caret = field->len;
        field->anchor = field->len;
        fieldSyncText(s, id);
    };
    write(picker.colorTarget, false);
    write(picker.colorHex, true);
    const int shown[7] = {
        static_cast<int>(colorInk(picker).r * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).g * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).b * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).a * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).h * 360.0f + 0.5f),
        static_cast<int>(colorInk(picker).s * 100.0f + 0.5f),
        static_cast<int>(colorInk(picker).v * 100.0f + 0.5f),
    };
    for (int i = 0; i < 7; ++i) {
        const uint16_t id = picker.colorNum[i];
        if (id == kUiNone) {
            continue;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr || s.focused == id) {
            continue;
        }
        uiWriteInt(field->bytes, field->len, shown[i]);
        field->caret = field->len;
        field->anchor = field->len;
        fieldSyncText(s, id);
    }
    setRange(s, picker.colorAlpha, colorInk(picker).a);
    if (publish) {
        publishColor(s, picker);
    }
    refreshColorChrome(s, picker);
    s.visualDirty = true;
}

void paintColor(UiState& s) {
    if (UiColorPickerComponent* picker = uiColorFind(s)) {
        applyColor(s, *picker, false);
    }
}

void dragWheel(UiState& s, UiColorPickerComponent& picker, uint16_t id, float x, float y) {
    const UiBox& b = s.box[id];
    if (id == picker.colorWheel) {
        const float cx = b.x + b.w * 0.5f;
        const float cy = b.y + b.h * 0.5f;
        const float dx = x - cx;
        const float dy = y - cy;
        const float radius = std::min(b.w, b.h) * 0.5f;
        float sat = radius > 1.0f ? std::sqrt(dx * dx + dy * dy) / radius : 0.0f;
        if (sat > 1.0f) {
            sat = 1.0f;
        }
        float hue = std::atan2(dy, dx) * 0.15915494f + 0.5f;
        if (hue < 0.0f) {
            hue += 1.0f;
        }
        if (hue >= 1.0f) {
            hue -= 1.0f;
        }
        colorInk(picker).h = hue;
        colorInk(picker).s = sat;
        applyColor(s, picker, true);
    } else if (id == picker.colorVal) {
        float t = b.h > 1.0f ? 1.0f - (y - b.y) / b.h : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        colorInk(picker).v = t;
        applyColor(s, picker, true);
    } else if (id == picker.colorAlpha) {
        float t = b.w > 1.0f ? (x - b.x) / b.w : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        colorInk(picker).a = t;
        applyColor(s, picker, true);
    }
}

void syncColor(UiState& s, UiColorPickerComponent& picker) {
    if (picker.book == nullptr) {
        return;
    }
    const ColorBook& b = *picker.book;
    if (b.gen != picker.colorSeen) {
        picker.colorSeen = b.gen;
        applyColor(s, picker, false, false);
    }
    if (colorInk(picker).present == picker.colorApplied) {
        return;
    }
    picker.colorApplied = colorInk(picker).present;
    if (picker.colorHost == 0) {
        if (colorInk(picker).present == 1) {
            placeColorMenu(s, picker);
            uiShow(s, picker.colorPanel, true);
        } else {
            uiShow(s, picker.colorPanel, false);
            if (colorInk(picker).present == 2 && picker.book != nullptr) {
                picker.book->present = 2;
            }
        }
    } else if (colorInk(picker).present != 2) {
        uiRequestClose(s);
    }
}

} // namespace burnhope
