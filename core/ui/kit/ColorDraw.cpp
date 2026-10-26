#include "ui/Bar.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "color/Color.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

#include <algorithm>
#include <numbers>
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
    const float ring = uiStyle(s, "color_marks", UiStyleKey::HandleSide); // wheel marker: white ring around a dot of the colour
    const float dot = uiStyle(s, "color_marks", UiStyleKey::Inner);
    const float tick = uiStyle(s, "color_marks", UiStyleKey::Width);      // value / alpha marker thickness
    const float over = uiStyle(s, "color_marks", UiStyleKey::Offset);     // how far a marker overhangs its bar
    const float corner = uiStyle(s, "color_marks", UiStyleKey::Radius);
    UiRampDraw drawn{};
    UiNodeBars bars{};
    if (ramp == UiRamp::HueWheel) {
        paint->token = UiToken::Custom;
        paintTint(*paint, kUiWhite);
        drawn.mode = 0;
        drawn.flags = kUiFlagWheel;
        const float radius = std::min(box.w, box.h) * 0.5f;
        const float ang = (colorInk(*color).h - 0.5f) * std::numbers::pi_v<float> * 2.0f;
        const float mx = box.x + box.w * 0.5f + std::cos(ang) * colorInk(*color).s * radius;
        const float my = box.y + box.h * 0.5f + std::sin(ang) * colorInk(*color).s * radius;
        pushColorBar(s, bars, mx - ring * 0.5f, my - ring * 0.5f, ring, ring, kUiWhite[0], kUiWhite[1], kUiWhite[2], 1.0f, ring * 0.5f);
        pushColorBar(s, bars, mx - dot * 0.5f, my - dot * 0.5f, dot, dot, colorInk(*color).r, colorInk(*color).g, colorInk(*color).b, 1.0f, dot * 0.5f);
    } else if (ramp == UiRamp::ValueRamp) {
        ColorLinear hue{};
        colorHsvToLinear(colorInk(*color).h, colorInk(*color).s, 1.0f, hue);
        paint->r = hue.r;
        paint->g = hue.g;
        paint->b = hue.b;
        drawn.mode = 1;
        drawn.flags = kUiFlagRamp;
        const float ty = box.y + (1.0f - colorInk(*color).v) * box.h;
        pushColorBar(s, bars, box.x - over, ty - tick * 0.5f, box.w + over * 2.0f, tick, kUiWhite[0], kUiWhite[1], kUiWhite[2], 1.0f, corner);
    } else if (ramp == UiRamp::Gradient) {
        drawn.mode = 6;
        drawn.flags = kUiFlagRamp;
        paint->token = UiToken::Custom;
        paintTint(*paint, kUiBlack);
        pushColorBar(s, bars, box.x, box.y, box.w, box.h, colorInk(*color).r, colorInk(*color).g, colorInk(*color).b, colorInk(*color).a, paint->radius);
    } else {
        paint->r = colorInk(*color).r;
        paint->g = colorInk(*color).g;
        paint->b = colorInk(*color).b;
        drawn.mode = 2;
        drawn.flags = kUiFlagRamp;
        const float tx = box.x + colorInk(*color).a * box.w;
        pushColorBar(s, bars, tx - tick * 0.5f, box.y - over, tick, box.h + over * 2.0f, kUiWhite[0], kUiWhite[1], kUiWhite[2], 1.0f, corner);
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
