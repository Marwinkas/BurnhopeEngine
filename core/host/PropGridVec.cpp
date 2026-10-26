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
#include <format>
#include <cstring>

namespace burnhope {
namespace {

// Shared body of the two- and three-axis rows. Three axes also get the link lock, the reset square and
// clickable axis caps (UiVecCtl); two axes keep inert caps.
UiPropVec3 propVecN(UiState& s, uint16_t host, const char* label, int n, const float* axes, float minV, float maxV, float step) {
    const bool full = n == 3;
    const PropMetrics m = propMetrics(s);
    const uint16_t slot = uiPropRow(s, host, label);
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.justify = UiJustify::Start;
    line.widthMode = UiSize::Percent;
    line.width = 100.0f;
    line.maxWMode = UiSize::Px;
    line.maxW = m.vectorMax;
    line.grow = 1.0f;
    line.shrink = 1.0f;
    line.gap = 0.0f;
    line.heightMode = UiSize::Px;
    line.height = m.slotH;
    const uint16_t row = uiPanel(s, slot, line, paintClear());
    // Axis fields share what lock, caps and gaps leave of the row equally (grow 1 from a zero basis).
    const char* letters[3] = {" ", " ", " "};
    UiPropVec3 out{};
    out.row = row;
    uint16_t* ids[3] = {&out.x, &out.y, &out.z};
    uint16_t caps[3] = {kUiNone, kUiNone, kUiNone};
    auto square = [&](uint8_t icon, uint16_t home, float side, float marginR) {
        UiFlex box{};
        box.widthMode = UiSize::Px;
        box.width = side;
        box.heightMode = UiSize::Px;
        box.height = side;
        box.shrink = 0.0f;
        box.minW = side;
        box.minH = side;
        box.marginR = marginR;
        box.self = UiAlignSelf::Center;
        UiPaint paint{};
        paint.token = UiToken::Custom;
        paint.r = s.look.text[0];
        paint.g = s.look.text[1];
        paint.b = s.look.text[2];
        paint.a = 0.0f;
        paint.borderW = 0.0f;
        paint.shadow = 0.0f;
        paint.radius = 0.0f;
        paint.icon = icon;
        box.pad = 0.0f;
        box.padL = 0.0f;
        box.padR = 0.0f;
        box.padT = 0.0f;
        box.padB = 0.0f;
        const uint16_t id = uiPanel(s, home, box, paint);
        if (id < s.count) {
            uiHover(s, id);
            uiSetFocusable(s, id);
            uiBind(s, id, propVecClick, &s);
        }
        return id;
    };
    if (full) {
        out.lock = square(static_cast<uint8_t>(UiIcon::LockOpen), row, m.lockSide, m.lockGap);
        if (out.lock < s.count) {
            s.ent[out.lock].set<UiVecCtl>({});
            if (auto* ctl = s.ent[out.lock].try_get_mut<UiVecCtl>()) {
                ctl->which = 3;
                ctl->home[0] = axes[0];
                ctl->home[1] = axes[1];
                ctl->home[2] = axes[2];
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        const float* ink = kUiAxis[i];
        UiFlex tag{};
        tag.widthMode = UiSize::Px;
        tag.width = m.badgeW;
        tag.heightMode = UiSize::Px;
        tag.height = m.vecH;
        tag.shrink = 0.0f;
        UiPaint axis{};
        axis.token = UiToken::Custom;
        axis.r = ink[0];
        axis.g = ink[1];
        axis.b = ink[2];
        axis.a = 1.0f;
        axis.radius = m.vecRadius;
        axis.radiusTr = 0.0f;
        axis.radiusBr = 0.0f;
        axis.radiusBl = m.vecRadius;
        if (full) {
            axis.hr = std::fmin(1.0f, ink[0] * m.badgeGain);
            axis.hg = std::fmin(1.0f, ink[1] * m.badgeGain);
            axis.hb = std::fmin(1.0f, ink[2] * m.badgeGain);
        }
        const uint16_t cap = uiPanel(s, row, tag, axis);
        caps[i] = cap;
        uiText(s, cap, letters[i]);
        uiTextStyle(s, cap, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        if (cap < s.count) {
            if (full) {
                uiHover(s, cap);
                uiSetFocusable(s, cap);
                uiBind(s, cap, propVecClick, &s);
            } else {
                uiSetPass(s, cap);
            }
        }
        UiFlex hold{};
        hold.widthMode = UiSize::Px;
        hold.width = 0.0f;
        hold.grow = 1.0f;
        hold.shrink = 1.0f;
        hold.minW = m.vecFieldMin;
        hold.align = UiAlign::Center;
        hold.heightMode = UiSize::Px;
        hold.height = m.vecH;
        const uint16_t box = uiPanel(s, row, hold, paintClear());
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step);
        uiFlexLimit(s, *ids[i], m.fieldW, m.slotH, 1.0f);
        propFillWidth(s, *ids[i]);
        uiNumberFill(s, *ids[i], false);
        // The field closes the pill: square on the badge side, rounded on the outer end.
        if (auto* paint = *ids[i] < s.count ? s.ent[*ids[i]].try_get_mut<UiPaint>() : nullptr) {
            paint->radius = 0.0f;
            paint->radiusTr = m.vecRadius;
            paint->radiusBr = m.vecRadius;
            paint->radiusBl = 0.0f;
        }
        // Axis numbers sit in a narrow box: the vector_field class gives them tighter side padding than a lone field.
        if (auto* field = *ids[i] < s.count ? s.ent[*ids[i]].try_get_mut<UiField>() : nullptr) {
            field->padL = m.vecPadL;
            field->padR = m.vecPadR;
        }
    }
    if (!full) {
        return out;
    }
    const uint16_t tail = uiPropTrail(s, row);
    out.reset = square(static_cast<uint8_t>(UiIcon::Reset), tail < s.count ? tail : row, m.slotH, 0.0f);
    UiVecCtl shared{};
    shared.field[0] = out.x;
    shared.field[1] = out.y;
    shared.field[2] = out.z;
    shared.home[0] = axes[0];
    shared.home[1] = axes[1];
    shared.home[2] = axes[2];
    auto stamp = [&](uint16_t id, uint8_t which) {
        if (id >= s.count) {
            return;
        }
        UiVecCtl ctl = shared;
        ctl.which = which;
        s.ent[id].set<UiVecCtl>(ctl);
    };
    for (int i = 0; i < n; ++i) {
        stamp(caps[i], static_cast<uint8_t>(i));
    }
    stamp(out.lock, 3);
    stamp(out.reset, 4);
    return out;
}

} // namespace

UiPropVec3 uiPropVec3(UiState& s, uint16_t host, const char* label, float x, float y, float z, float minV, float maxV, float step) {
    const float axes[3] = {x, y, z};
    return propVecN(s, host, label, 3, axes, minV, maxV, step);
}

UiPropVec2 uiPropVec2(UiState& s, uint16_t host, const char* label, float x, float y, float minV, float maxV, float step) {
    const float axes[2] = {x, y};
    const UiPropVec3 row = propVecN(s, host, label, 2, axes, minV, maxV, step);
    return {.x = row.x, .y = row.y, .row = row.row};
}

} // namespace burnhope
