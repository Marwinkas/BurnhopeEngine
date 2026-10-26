#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/Template.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Label.hpp"

namespace burnhope {

uint16_t uiLabel(UiState& s, uint16_t parent, const char* text) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/label");
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text);
    return id;
}

void uiLabelBin(UiState& s) {
    uiBinRole(s, kBinSlotLabel, "label", static_cast<uint8_t>(UiToken::Text), kBinRoleClear);
}

} // namespace burnhope
