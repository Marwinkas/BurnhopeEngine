#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Interaction.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Search.hpp"
#include "host/PropGrid.hpp"
#include "host/PropGridMetrics.hpp"
#include "host/PropRoll.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"

#include <cmath>
#include <cstring>

namespace burnhope {

namespace {

float propMenuWidth(const UiState& s, uint16_t menu, const char* value, float chrome) {
    float widest = 0.0f;
    auto consider = [&](const char* text, uint8_t len) {
        if (text == nullptr || len == 0) {
            return;
        }
        const float measured = uiMeasure(s, text, len, len);
        if (measured > widest) {
            widest = measured;
        }
    };
    if (value != nullptr) {
        uint8_t len = 0;
        while (value[len] != '\0' && len < 48) {
            ++len;
        }
        consider(value, len);
    }
    if (menu < s.count) {
        propEachChild(s, menu, [&](uint16_t child) {
            const UiText* text = s.ent[child].try_get<UiText>();
            if (text != nullptr) {
                consider(text->bytes, text->len);
            }
            return true;
        });
    }
    return widest + chrome;
}

} // namespace

uint16_t uiPropCheck(UiState& s, uint16_t host, const char* label, uint8_t kind, uint8_t group, bool on) {
    const uint16_t value = uiPropRow(s, host, label);
    const uint16_t mark = uiCheck(s, value, "", kind, group, on);
    // The mark is a bare box in the value slot: the prop_check class drops the label padding of a check row.
    (void)uiApplyTemplate(s, mark, uiTemplateFind(s, "core/checkbox"), "prop_check");
    return mark;
}

uint16_t uiPropNumber(UiState& s, uint16_t host, const char* label, float value, float minV, float maxV, float step) {
    const PropMetrics m = propMetrics(s);
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiNumber(s, slot, value, minV, maxV, step);
    uiFlexLimit(s, id, m.fieldW, m.slotH, 0.0f);
    propFillWidth(s, id);
    return id;
}

uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiMenuButton(s, slot, value, menu);
    const float width = propMenuWidth(s, menu, value, propMetrics(s).menuChrome);
    uiFlexLimit(s, id, width, 0.0f, 1.0f);
    if (menu < s.count) {
        s.ent[menu].add<UiMenuSlide>();
        uiSetSize(s, menu, width, -1.0f);
    }
    return id;
}

uint16_t uiPropSwatch(UiState& s, uint16_t host, const char* label, float r, float g, float b) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiSpawnTemplate(s, slot, "core/panel", "prop_swatch");
    if (auto* paint = id < s.count ? s.ent[id].try_get_mut<UiPaint>() : nullptr) {
        paintFillRgb(*paint, UiRgb{r, g, b});
    }
    if (id < s.count) {
        uiHover(s, id);
        uiSetFocusable(s, id);
    }
    return id;
}

} // namespace burnhope
