#include "ui/Bar.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "color/Color.hpp"
#include "ui/State.hpp"

#include <algorithm>
#include <cmath>

namespace burnhope {

namespace {

void pushColorBar(UiState& s, UiNodeBars& bars, float x, float y, float w, float h, float r, float g, float b, float a, float radius) {
    if (w < 0.5f || h < 0.5f) {
        return;
    }
    UiBar bar{};
    bar.x = x;
    bar.y = y;
    bar.w = w;
    bar.h = h;
    bar.r = r;
    bar.g = g;
    bar.b = b;
    bar.a = a;
    bar.radius = radius;
    bar.on = 1;
    uiNodeBarPush(s, bars, bar);
}

void colorPlaceNode(UiState& s, uint16_t id, const UiColorPickerComponent* color) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    const UiRamp ramp = paint->ramp;
    if (ramp != UiRamp::HueWheel && ramp != UiRamp::ValueRamp && ramp != UiRamp::Gradient && ramp != UiRamp::Alpha) {
        return;
    }
    const UiBox& box = s.box[id];
    UiRampDraw drawn{};
    UiNodeBars bars{};
    if (ramp == UiRamp::HueWheel) {
        paint->r = 1.0f;
        paint->g = 1.0f;
        paint->b = 1.0f;
        drawn.mode = 0;
        drawn.flags = kUiFlagWheel;
        const float radius = std::min(box.w, box.h) * 0.5f;
        const float ang = (colorInk(*color).h - 0.5f) * 6.2831853f;
        const float mx = box.x + box.w * 0.5f + std::cos(ang) * colorInk(*color).s * radius;
        const float my = box.y + box.h * 0.5f + std::sin(ang) * colorInk(*color).s * radius;
        pushColorBar(s, bars, mx - 7.0f, my - 7.0f, 14.0f, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f, 7.0f);
        pushColorBar(s, bars, mx - 4.0f, my - 4.0f, 8.0f, 8.0f, colorInk(*color).r, colorInk(*color).g, colorInk(*color).b, 1.0f, 4.0f);
    } else if (ramp == UiRamp::ValueRamp) {
        ColorLinear hue{};
        colorHsvToLinear(colorInk(*color).h, colorInk(*color).s, 1.0f, hue);
        paint->r = hue.r;
        paint->g = hue.g;
        paint->b = hue.b;
        drawn.mode = 1;
        drawn.flags = kUiFlagRamp;
        const float ty = box.y + (1.0f - colorInk(*color).v) * box.h;
        pushColorBar(s, bars, box.x - 2.0f, ty - 3.0f, box.w + 4.0f, 6.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f);
    } else if (ramp == UiRamp::Gradient) {
        drawn.mode = 6;
        drawn.flags = kUiFlagRamp;
        paint->r = 0.0f;
        paint->g = 0.0f;
        paint->b = 0.0f;
        pushColorBar(s, bars, box.x, box.y, box.w, box.h, colorInk(*color).r, colorInk(*color).g, colorInk(*color).b, colorInk(*color).a, paint->radius);
    } else {
        paint->r = colorInk(*color).r;
        paint->g = colorInk(*color).g;
        paint->b = colorInk(*color).b;
        drawn.mode = 2;
        drawn.flags = kUiFlagRamp;
        const float tx = box.x + colorInk(*color).a * box.w;
        pushColorBar(s, bars, tx - 3.0f, box.y - 2.0f, 6.0f, box.h + 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f);
    }
    if (auto* live = s.ent[id].try_get_mut<UiRampDraw>()) {
        *live = drawn;
    } else {
        s.ent[id].set<UiRampDraw>(drawn);
    }
    if (auto* live = s.ent[id].try_get_mut<UiNodeBars>()) {
        *live = bars;
    } else if (bars.n != 0) {
        s.ent[id].set<UiNodeBars>(bars);
    }
}

} // namespace

// The four ramp nodes of each picker are repainted from its book; no scan over the node table.
void colorPlace(UiState& s) {
    uiColorEach(s, [&s](uint16_t at) {
        const UiColorPickerComponent* color = s.ent[at].try_get<UiColorPickerComponent>();
        if (color == nullptr) {
            return;
        }
        for (const uint16_t id : {color->colorWheel, color->colorVal, color->colorSwatch, color->colorAlpha}) {
            colorPlaceNode(s, id, color);
        }
    });
}

} // namespace burnhope
