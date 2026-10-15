#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/Text.hpp"

namespace burnhope {

uint16_t uiLabel(UiState& s, uint16_t parent, const char* text) {
    UiFlex flex{};
    flex.shrink = 0.0f;
    UiPaint ink = paintToken(s.look, UiToken::Text, 0.0f);
    ink.a = 0.0f;
    const uint16_t id = uiPanel(s, parent, flex, ink);
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text);
    s.ent[id].add<UiPass>();
    return id;
}

} // namespace burnhope
