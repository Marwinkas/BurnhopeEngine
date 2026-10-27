#include "host/PropGridMetrics.hpp"

#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

namespace burnhope {

PropMetrics propMetrics(const UiState& s) {
    PropMetrics m;
    m.slotH = uiStyle(s, "prop_row", UiStyleKey::Inner);
    m.fieldW = uiStyle(s, "prop_row", UiStyleKey::FieldW);
    m.hoverAlpha = uiStyle(s, "prop_row", UiStyleKey::HoverAlpha);
    m.menuChrome = uiStyle(s, "dropdown_trigger", UiStyleKey::Chrome);
    m.vecPadL = uiStyle(s, "vector_field", UiStyleKey::PadL);
    m.vecPadR = uiStyle(s, "vector_field", UiStyleKey::PadR);
    return m;
}

void propFillWidth(UiState& s, uint16_t id) {
    uiSetWidthPercent(s, id, 100.0f);
    uiSetGrow(s, id, 1.0f, 1.0f, 0.0f);
}

} // namespace burnhope
