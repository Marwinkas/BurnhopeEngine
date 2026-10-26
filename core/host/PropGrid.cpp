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

#include <cmath>
#include <cstring>

namespace burnhope {
namespace {

UiPaint paintHair(const UiState& s, const PropMetrics& m) {
    UiStyleClass hair;
    const UiToken token = uiStyleResolve(s, "prop_hair", hair) ? hair.bg : UiToken::Item;
    UiPaint paint = paintToken(s.look, token, 0.0f);
    paint.a = m.hairAlpha;
    return paint;
}

void propHair(UiState& s, uint16_t column, const PropMetrics& m) {
    if (column >= s.count || uiNodeChildCount(s, column) == 0) {
        return;
    }
    UiFlex line{};
    line.heightMode = UiSize::Px;
    line.height = m.hairH;
    line.shrink = 0.0f;
    (void)uiPanel(s, column, line, paintHair(s, m));
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
    UiFlex column{};
    column.direction = UiFlexDir::Column;
    column.position = UiPosition::Relative;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    const uint16_t id = uiPanel(s, parent, column, paintClear());
    if (id == kUiNone) {
        return id;
    }
    return id;
}

uint16_t uiPropRow(UiState& s, uint16_t host, const char* label) {
    const PropMetrics m = propMetrics(s);
    propHair(s, host, m);
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.widthMode = UiSize::Percent;
    line.width = 100.0f;
    line.heightMode = UiSize::Px;
    line.height = m.rowH;
    line.padT = m.padV;
    line.padB = m.padV;
    line.padL = 0.0f;
    line.padR = m.margin;
    line.shrink = 0.0f;
    line.overflow = UiOverflow::Hidden;
    const uint16_t row = uiPanel(s, host, line, paintClear());
    if (row < s.count) {
        s.ent[row].add<UiRowHover>();
        if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
            paint->hoverKids = 1;
            paint->hr = s.look.text[0];
            paint->hg = s.look.text[1];
            paint->hb = s.look.text[2];
            paint->ha = m.hoverAlpha;
        }
    }
    UiFlex name{};
    name.widthMode = UiSize::Px;
    name.width = m.labelW;
    name.heightMode = UiSize::Px;
    name.height = m.slotH;
    name.shrink = 0.0f;
    name.overflow = UiOverflow::Hidden;
    name.padR = m.labelPad;
    const uint16_t caption = uiPanel(s, row, name, paintLabel(s.look));
    uiText(s, caption, label != nullptr ? label : "");
    uiTextStyle(s, caption, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (caption < s.count) {
        propLinkLabel(s, caption);
        uiSetPass(s, caption);
    }
    UiFlex value{};
    value.direction = UiFlexDir::Row;
    value.align = UiAlign::Center;
    value.justify = UiJustify::Start;
    value.grow = 0.0f;
    value.shrink = 1.0f;
    value.widthMode = UiSize::Percent;
    value.width = 100.0f;
    value.maxWMode = UiSize::Px;
    value.maxW = m.controlMax + m.gap;
    value.heightMode = UiSize::Px;
    value.height = m.slotH;
    value.padL = m.gap;
    value.overflow = UiOverflow::Hidden;
    const uint16_t slot = uiPanel(s, row, value, paintClear());
    UiFlex trail{};
    trail.direction = UiFlexDir::Row;
    trail.align = UiAlign::Center;
    trail.justify = UiJustify::End;
    trail.widthMode = UiSize::Px;
    trail.width = m.trailW;
    trail.heightMode = UiSize::Px;
    trail.height = m.slotH;
    trail.grow = 1.0f;
    trail.shrink = 0.0f;
    trail.overflow = UiOverflow::Hidden;
    const uint16_t tail = uiPanel(s, row, trail, paintClear());
    if (tail < s.count) {
        s.ent[tail].add<UiPropTrail>();
        uiSetPass(s, tail);
    }
    return slot;
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

void uiPropRowHeight(UiState& s, uint16_t valueSlot, float rowH, float innerH) {
    const uint16_t row = uiPropRowOf(s, valueSlot);
    auto* outer = row < s.count ? s.ent[row].try_get_mut<UiFlex>() : nullptr;
    auto* inner = valueSlot < s.count ? s.ent[valueSlot].try_get_mut<UiFlex>() : nullptr;
    if (outer == nullptr || inner == nullptr) {
        return;
    }
    if (outer->heightMode == UiSize::Px && outer->height == rowH && inner->heightMode == UiSize::Px && inner->height == innerH) {
        return;
    }
    outer->heightMode = UiSize::Px;
    outer->height = rowH;
    inner->heightMode = UiSize::Px;
    inner->height = innerH;
    inner->overflow = UiOverflow::Visible;
    s.layoutDirty = true;
}

void uiPropValueMax(UiState& s, uint16_t valueSlot, float maxW) {
    auto* flex = valueSlot < s.count ? s.ent[valueSlot].try_get_mut<UiFlex>() : nullptr;
    if (flex != nullptr && flex->maxW != maxW) {
        flex->maxWMode = UiSize::Px;
        flex->maxW = maxW;
        s.layoutDirty = true;
    }
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
