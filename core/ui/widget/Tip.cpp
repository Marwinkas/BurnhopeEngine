#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Tip.hpp"

namespace burnhope {

uint16_t uiTip(UiState& s, uint16_t parent) {
    UiFlex flex{};
    flex.position = UiPosition::Absolute;
    flex.pad = 6.0f;
    flex.shrink = 0.0f;
    flex.widthMode = UiSize::Px;
    flex.heightMode = UiSize::Px;
    flex.width = 8.0f;
    flex.height = 8.0f;
    UiPaint body = paintToken(s.look, UiToken::Menu, s.look.radiusSm);
    body.shadow = 10.0f;
    body.borderW = 1.0f;
    body.br = s.look.muted[0];
    body.bg = s.look.muted[1];
    body.bb = s.look.muted[2];
    const uint16_t id = uiPanel(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
    s.ent[id].set<UiText>({});
    addText(s, id, "");
    uiShow(s, id, false);
    return id;
}

void uiTipAt(UiState& s, uint16_t id, const char* text, float x, float y) {
    if (id >= s.count) {
        return;
    }
    addText(s, id, text);
    const UiText* line = s.ent[id].try_get<UiText>();
    float w = line != nullptr ? uiMeasure(s, line->bytes, line->len, line->len) + 16.0f : 16.0f;
    if (w < 24.0f) {
        w = 24.0f;
    }
    const float h = s.look.glyphH + 12.0f;
    uiPlace(s, id, true, x, y, w, h);
    uiShow(s, id, true);
}

} // namespace burnhope
