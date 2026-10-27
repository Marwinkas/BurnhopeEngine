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

void propHair(UiState& s, uint16_t column) {
    if (column >= s.count || uiNodeChildCount(s, column) == 0) {
        return;
    }
    (void)uiSpawnTemplate(s, column, "core/panel", "prop_hair");
}

} // namespace

void propLinkLabel(UiState& s, uint16_t caption) {
    UiPropRoll& roll = s.world.ensure<UiPropRoll>();
    uint16_t next = roll.labels;
    if (next >= s.count || !s.ent[next].is_alive() || !s.ent[next].has<UiPropLabel>()) {
        next = kUiNone;
    }
    s.ent[caption].set<UiPropLabel>({next});
    roll.labels = caption;
}

bool propUnder(const UiState& s, uint16_t id, uint16_t host) {
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == host) {
            return true;
        }
    }
    return false;
}

uint16_t propBar(const UiState& s) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    const uint16_t bar = roll != nullptr ? roll->bar : kUiNone;
    if (bar >= s.count || !s.ent[bar].is_alive() || !s.ent[bar].has<UiPropFilterBar>()) {
        return kUiNone;
    }
    return bar;
}

uint16_t uiPropHost(UiState& s, uint16_t parent) {
    return uiSpawnTemplate(s, parent, "core/prop_host");
}

uint16_t uiPropRow(UiState& s, uint16_t host, const char* label) {
    propHair(s, host);
    const UiTemplateId tpl = uiTemplateFind(s, "core/prop_row");
    const uint16_t row = uiSpawnTemplate(s, host, tpl);
    if (row == kUiNone) {
        return kUiNone;
    }
    s.ent[row].add<UiRowHover>();
    if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
        paint->hoverKids = 1;
        paintHoverToken(s.look, *paint, UiToken::Text, propMetrics(s).hoverAlpha);
    }
    const uint16_t caption = uiTemplateSlot(s, tpl, row, "caption");
    uiText(s, caption, label != nullptr ? label : "");
    propLinkLabel(s, caption);
    s.ent[uiTemplateSlot(s, tpl, row, "trail")].add<UiPropTrail>();
    return uiTemplateSlot(s, tpl, row, "slot");
}

uint16_t uiPropTrail(const UiState& s, uint16_t node) {
    uint16_t row = node;
    while (row < s.count && !s.ent[row].has<UiRowHover>()) {
        row = s.parentOf[row];
    }
    if (row >= s.count) {
        return kUiNone;
    }
    uint16_t found = kUiNone;
    propEachChild(s, row, [&](uint16_t child) {
        if (s.ent[child].has<UiPropTrail>()) {
            found = child;
            return false;
        }
        return true;
    });
    return found;
}

uint16_t uiPropSlotOf(const UiState& s, uint16_t control) {
    return control < s.count ? s.parentOf[control] : kUiNone;
}

uint16_t uiPropRowOf(const UiState& s, uint16_t valueSlot) {
    return valueSlot < s.count ? s.parentOf[valueSlot] : kUiNone;
}

void uiPropRowShow(UiState& s, uint16_t valueSlot, bool on) {
    const uint16_t row = uiPropRowOf(s, valueSlot);
    if (row < s.count) {
        uiShow(s, row, on);
    }
}

bool uiPropLinked(const UiState& s, uint16_t lock) {
    if (lock >= s.count) {
        return false;
    }
    const UiVecCtl* ctl = s.ent[lock].try_get<UiVecCtl>();
    return ctl != nullptr && ctl->linked != 0;
}

} // namespace burnhope
