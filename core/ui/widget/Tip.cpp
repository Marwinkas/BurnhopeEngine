#include "ui/widget/Detail.hpp"

namespace burnhope {

uint16_t uiTip(UiState& s, uint16_t parent) {
    UiFlex flex{};
    flex.position = 1;
    flex.pad = 6.0f;
    flex.shrink = 0.0f;
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = 8.0f;
    flex.height = 8.0f;
    UiPaint body = paint(UiRole::Panel, 0.08f, 0.09f, 0.12f, 4.0f);
    body.shadow = 10.0f;
    body.borderW = 1.0f;
    body.br = 0.32f;
    body.bg = 0.36f;
    body.bb = 0.46f;
    const uint16_t id = spawn(s, parent, flex, body);
    if (id == kUiNone || s.count >= kUiCap) {
        return id;
    }
    const uint16_t label = spawn(s, id, UiFlex{}, paint(UiRole::Label, 0.93f, 0.94f, 0.96f));
    addText(s, label, "");
    s.labelOf[id] = label;
    uiShow(s, id, false);
    return id;
}

void uiTipAt(UiState& s, uint16_t id, const char* text, float x, float y) {
    if (id >= s.count) {
        return;
    }
    const uint16_t label = s.labelOf[id];
    if (label >= s.count) {
        return;
    }
    addText(s, label, text);
    const UiText* line = s.ent[label].try_get<UiText>();
    float w = line != nullptr ? uiMeasure(s, line->bytes, line->len, line->len) + 16.0f : 16.0f;
    if (w < 24.0f) {
        w = 24.0f;
    }
    const float h = s.look.glyphH + 12.0f;
    uiPlace(s, id, true, x, y, w, h);
    uiShow(s, id, true);
}

} // namespace burnhope
