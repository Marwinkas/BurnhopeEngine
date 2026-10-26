#include "host/PropGridMetrics.hpp"

#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

namespace burnhope {

PropMetrics propMetrics(const UiState& s) {
    PropMetrics m;
    m.rowH = uiStyle(s, "prop_row", UiStyleKey::Height);
    m.slotH = uiStyle(s, "prop_row", UiStyleKey::Inner);
    m.padV = uiStyle(s, "prop_row", UiStyleKey::Pad);
    m.margin = uiStyle(s, "prop_row", UiStyleKey::PadR);
    m.labelW = uiStyle(s, "prop_row", UiStyleKey::LabelW);
    m.labelPad = uiStyle(s, "prop_row", UiStyleKey::LabelPad);
    m.gap = uiStyle(s, "prop_row", UiStyleKey::Gap);
    m.fieldW = uiStyle(s, "prop_row", UiStyleKey::FieldW);
    m.trailW = uiStyle(s, "prop_row", UiStyleKey::Trail);
    m.hoverAlpha = uiStyle(s, "prop_row", UiStyleKey::HoverAlpha);
    m.controlMax = uiStyle(s, "prop_row", UiStyleKey::MaxW);
    m.vectorMax = uiStyle(s, "vector_row", UiStyleKey::MaxW);
    m.lockGap = uiStyle(s, "vector_row", UiStyleKey::LockGap);
    m.badgeW = uiStyle(s, "vector_row", UiStyleKey::BadgeW);
    m.badgeGain = uiStyle(s, "vector_row", UiStyleKey::HoverGain);
    m.lockSide = uiStyle(s, "vector_lock", UiStyleKey::Width);
    m.vecH = uiStyle(s, "vector_field", UiStyleKey::Height);
    m.vecRadius = uiStyle(s, "vector_field", UiStyleKey::Radius);
    m.vecFieldMin = uiStyle(s, "vector_field", UiStyleKey::MinW);
    m.vecPadL = uiStyle(s, "vector_field", UiStyleKey::PadL);
    m.vecPadR = uiStyle(s, "vector_field", UiStyleKey::PadR);
    m.swatchBorder = uiStyle(s, "swatch", UiStyleKey::Border);
    m.checkSide = uiStyle(s, "check_box", UiStyleKey::Width);
    m.menuChrome = uiStyle(s, "dropdown_trigger", UiStyleKey::Chrome);
    m.hairH = uiStyle(s, "prop_hair", UiStyleKey::Height);
    m.hairAlpha = uiStyle(s, "prop_hair", UiStyleKey::Alpha);
    return m;
}

} // namespace burnhope
