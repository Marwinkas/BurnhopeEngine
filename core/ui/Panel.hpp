#pragma once

#include "ui/Canvas.hpp"

namespace burnhope {

// Невиртуальная панель для кода приложения. Горячий кадр её не вызывает.
struct Panel {
    Canvas* canvas = nullptr;
    uint16_t id = kUiNone;

    static Panel make(Canvas& canvas, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
    static Panel at(Canvas& canvas, uint16_t id);
    static Panel find(Canvas& canvas, const char* name);
    [[nodiscard]] Panel child(const UiFlex& flex, const UiPaint& paint) const;
    [[nodiscard]] Panel label(const char* text) const;
    [[nodiscard]] Panel textButton(const char* text, int8_t tag) const;
    [[nodiscard]] Panel button(const char* text, int8_t tag, float r, float g, float b, uint8_t icon) const;
    [[nodiscard]] Panel field(const char* text, uint8_t filter, bool clear) const;
    [[nodiscard]] Panel check(const char* text) const;
    [[nodiscard]] Panel radio(const char* text, uint8_t group) const;
    [[nodiscard]] Panel slider(float value, float minV, float maxV) const;
    [[nodiscard]] Panel spin(float value, float minV, float maxV, float step) const;
    [[nodiscard]] Panel curve() const;
    [[nodiscard]] Panel gradient() const;
    void setText(const char* text) const;
    void name(const char* text) const;
    void color(float r, float g, float b, float a) const;
    void animate(float r, float g, float b, float a) const;
    void transform(float angle, float scaleX, float scaleY) const;
    void filter(float bright, float contrast) const;
    void blur(float blur) const;
};

} // namespace burnhope
