#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Slider.hpp"

namespace burnhope {

void Panel::name(const char* text) const {
    if (state != nullptr) {
        uiName(*state, id, text);
    }
}

Panel Panel::find(UiState& state, const char* name) {
    return at(state, uiFindName(state, name));
}

Panel Panel::at(UiState& state, uint16_t id) {
    Panel panel{};
    panel.state = &state;
    panel.id = id;
    return panel;
}

Panel Panel::make(UiState& state, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    Panel panel{};
    panel.state = &state;
    panel.id = uiNode(state, parent, flex, paint);
    return panel;
}

Panel Panel::child(const UiFlex& flex, const UiPaint& paint) const {
    Panel panel{};
    panel.state = state;
    if (state != nullptr) {
        panel.id = uiNode(*state, id, flex, paint);
    }
    return panel;
}

Panel Panel::label(const char* text) const {
    const UiLook look = state != nullptr ? state->look : UiLook{};
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = look.glyphH;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Label);
    paint.token = static_cast<uint8_t>(UiToken::Text);
    paint.a = 1.0f;
    paint.r = look.text[0];
    paint.g = look.text[1];
    paint.b = look.text[2];
    Panel out = child(flex, paint);
    out.setText(text);
    if (state != nullptr) {
        uiStamp(*state, out.id, UiToken::Text);
    }
    return out;
}

Panel Panel::textButton(const char* text, int8_t tag) const {
    Panel box{};
    box.state = state;
    if (state == nullptr) {
        return box;
    }
    UiButtonConfig cfg{};
    cfg.label = text;
    cfg.height = state->look.glyphH + 6.0f;
    box.id = uiButton(*state, id, cfg);
    if (box.id < state->count) {
        if (auto* paint = state->ent[box.id].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
            paint->tag = tag;
            uiNoteChrome(*state, box.id);
        }
    }
    return box;
}

Panel Panel::button(const char* text, int8_t tag, float r, float g, float b, uint8_t icon) const {
    Panel box{};
    box.state = state;
    if (state == nullptr) {
        return box;
    }
    const bool labeled = text != nullptr && text[0] != '\0';
    UiButtonConfig cfg{};
    cfg.label = labeled ? text : nullptr;
    cfg.height = 30.0f;
    cfg.width = labeled ? 0.0f : 30.0f;
    box.id = uiButton(*state, id, cfg);
    if (box.id < state->count) {
        if (auto* paint = state->ent[box.id].try_get_mut<UiPaint>()) {
            paint->r = r;
            paint->g = g;
            paint->b = b;
            paint->a = 1.0f;
            paint->tag = tag;
            paint->icon = icon;
            uiNoteChrome(*state, box.id);
        }
    }
    return box;
}

Panel Panel::field(const char* text, uint8_t filter, bool clear) const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiField(*state, id, text, filter, clear);
    }
    return box;
}

Panel Panel::check(const char* text) const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiCheck(*state, id, text, 0, 0, false);
    }
    return box;
}

Panel Panel::radio(const char* text, uint8_t group) const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiCheck(*state, id, text, 1, group, false);
    }
    return box;
}

Panel Panel::slider(float value, float minV, float maxV) const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiSlider(*state, id, value, minV, maxV);
    }
    return box;
}

Panel Panel::spin(float value, float minV, float maxV, float step) const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiSpin(*state, id, value, minV, maxV, step);
    }
    return box;
}

Panel Panel::curve() const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiCurve(*state, id);
    }
    return box;
}

Panel Panel::gradient() const {
    Panel box{};
    box.state = state;
    if (state != nullptr) {
        box.id = uiGradient(*state, id);
    }
    return box;
}

void Panel::setText(const char* text) const {
    if (state != nullptr) {
        uiText(*state, id, text);
    }
}

void Panel::color(float r, float g, float b, float a) const {
    if (state == nullptr || id >= state->count) {
        return;
    }
    auto* paint = state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->r = r;
    paint->g = g;
    paint->b = b;
    paint->a = a;
    paint->token = static_cast<uint8_t>(UiToken::Custom);
    state->visualDirty = true;
}

void Panel::animate(float r, float g, float b, float a) const {
    if (state != nullptr) {
        uiMotionTo(*state, id, r, g, b, a);
    }
}

void Panel::transform(float angle, float scaleX, float scaleY) const {
    if (state == nullptr || id >= state->count) {
        return;
    }
    auto* paint = state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->angle = angle;
    paint->scaleX = scaleX;
    paint->scaleY = scaleY;
    state->visualDirty = true;
}

void Panel::filter(float bright, float contrast) const {
    if (state == nullptr || id >= state->count) {
        return;
    }
    auto* paint = state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->bright = bright;
    paint->contrast = contrast;
    state->visualDirty = true;
}

void Panel::blur(float blur) const {
    if (state == nullptr || id >= state->count) {
        return;
    }
    auto* paint = state->ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->blur = blur < 0.0f ? 0.0f : (blur > 1.0f ? 1.0f : blur);
    state->visualDirty = true;
}

} // namespace burnhope
