#include "ui/Panel.hpp"

namespace burnhope {

Panel Panel::at(Canvas& canvas, uint16_t id) {
    Panel panel{};
    panel.canvas = &canvas;
    panel.id = id;
    return panel;
}

Panel Panel::make(Canvas& canvas, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    Panel panel{};
    panel.canvas = &canvas;
    panel.id = canvasNode(canvas, parent, flex, paint);
    return panel;
}

Panel Panel::child(const UiFlex& flex, const UiPaint& paint) const {
    Panel panel{};
    panel.canvas = canvas;
    if (canvas != nullptr) {
        panel.id = canvasNode(*canvas, id, flex, paint);
    }
    return panel;
}

Panel Panel::label(const char* text) const {
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = canvas != nullptr ? canvasLook(*canvas).glyphH : 18.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Label);
    paint.token = static_cast<uint8_t>(UiToken::Text);
    paint.a = 1.0f;
    paint.r = 0.93f;
    paint.g = 0.94f;
    paint.b = 0.96f;
    Panel out = child(flex, paint);
    out.setText(text);
    if (canvas != nullptr) {
        canvasStamp(*canvas, out.id, UiToken::Text);
    }
    return out;
}

Panel Panel::textButton(const char* text, int8_t tag) const {
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = canvas != nullptr ? canvasLook(*canvas).glyphH + 6.0f : 24.0f;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Button);
    paint.a = 0.0f;
    paint.tag = tag;
    paint.r = 0.80f;
    paint.g = 0.86f;
    paint.b = 0.95f;
    Panel button = child(flex, paint);
    button.setText(text);
    return button;
}

Panel Panel::button(const char* text, int8_t tag, float r, float g, float b, uint8_t icon) const {
    const bool labeled = text != nullptr && text[0] != '\0';
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 30.0f;
    flex.shrink = 0.0f;
    flex.align = 1;
    if (!labeled) {
        flex.widthMode = static_cast<uint8_t>(UiSize::Px);
        flex.width = 30.0f;
    }
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Button);
    paint.r = r;
    paint.g = g;
    paint.b = b;
    paint.a = 1.0f;
    paint.radius = 6.0f;
    paint.tag = tag;
    paint.icon = icon;
    Panel button = child(flex, paint);
    if (labeled) {
        button.setText(text);
        if (canvas != nullptr) {
            canvasGrow(*canvas, button.id, icon != 0 ? 28.0f : 16.0f, 30.0f);
        }
    }
    return button;
}

Panel Panel::field(const char* text, uint8_t filter, bool clear) const {
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    flex.width = 100.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 36.0f;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Field);
    paint.r = 0.14f;
    paint.g = 0.15f;
    paint.b = 0.19f;
    paint.a = 1.0f;
    paint.radius = 6.0f;
    paint.borderW = 1.0f;
    paint.br = 0.32f;
    paint.bg = 0.38f;
    paint.bb = 0.52f;
    Panel box = child(flex, paint);
    if (canvas != nullptr) {
        canvasField(*canvas, box.id, text, filter, clear);
    }
    return box;
}

Panel Panel::check(const char* text) const {
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    flex.width = 100.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 32.0f;
    flex.shrink = 0.0f;
    flex.align = 1;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Check);
    paint.r = 0.16f;
    paint.g = 0.17f;
    paint.b = 0.21f;
    paint.a = 1.0f;
    paint.radius = 6.0f;
    Panel box = child(flex, paint);
    if (canvas != nullptr) {
        box.setText(text);
        canvasCheck(*canvas, box.id, 0, 0, false);
    }
    return box;
}

Panel Panel::radio(const char* text, uint8_t group) const {
    Panel box = check(text);
    if (canvas != nullptr) {
        canvasCheck(*canvas, box.id, 1, group, false);
    }
    return box;
}

void Panel::setText(const char* text) const {
    if (canvas != nullptr) {
        canvasText(*canvas, id, text);
    }
}

void Panel::color(float r, float g, float b, float a) const {
    if (canvas != nullptr) {
        canvasSetPaint(*canvas, id, r, g, b, a);
    }
}

void Panel::animate(float r, float g, float b, float a) const {
    if (canvas != nullptr) {
        canvasAnimate(*canvas, id, r, g, b, a);
    }
}

} // namespace burnhope
