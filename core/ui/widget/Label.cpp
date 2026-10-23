#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Label.hpp"

namespace burnhope {

uint16_t uiLabel(UiState& s, uint16_t parent, const char* text) {
    UiFlex flex{};
    flex.shrink = 0.0f;
    UiPaint ink = paintLabel(s.look);
    const uint16_t id = uiPanel(s, parent, flex, ink);
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text);
    s.ent[id].add<UiPass>();
    return id;
}

void uiLabelBin(UiState& s) {
    uiBinRole(s, kBinSlotLabel, "label", static_cast<uint8_t>(UiToken::Text), kBinRoleClear);
}

} // namespace burnhope
