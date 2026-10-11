#pragma once

#include "ui/widget/Detail.hpp"

namespace burnhope {

// Layout sugar. Talks to UiState and the widget functions, never to Canvas or Vulkan.
struct Panel {
    UiState* state = nullptr;
    uint16_t id = kUiNone;

    static Panel make(UiState& state, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
    static Panel at(UiState& state, uint16_t id);
    static Panel find(UiState& state, const char* name);
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
