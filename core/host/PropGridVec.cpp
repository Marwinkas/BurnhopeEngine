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
#include <format>
#include <cstring>

namespace burnhope {
namespace {

// Shared body of the two- and three-axis rows (blueprints vec_row2 / vec_row3). Three axes also get the link lock, the
// reset square and clickable axis caps (UiVecCtl); two axes keep inert caps.
UiPropVec3 propVecN(UiState& s, uint16_t host, const char* label, int n, const float* axes, float minV, float maxV, float step) {
    const bool full = n == 3;
    const PropMetrics m = propMetrics(s);
    const uint16_t slot = uiPropRow(s, host, label);
    const UiTemplateId tpl = uiTemplateFind(s, full ? "core/vec_row3" : "core/vec_row2");
    const uint16_t row = uiSpawnTemplate(s, slot, tpl);
    UiPropVec3 out{};
    out.row = row;
    if (row == kUiNone) {
        return out;
    }
    // The axis fields share what lock, caps and gaps leave of the row equally.
    uiSetGrow(s, row, 1.0f, 1.0f);
    static constexpr const char* kHold[3] = {"holdx", "holdy", "holdz"};
    static constexpr const char* kCap[3] = {"capx", "capy", "capz"};
    uint16_t* ids[3] = {&out.x, &out.y, &out.z};
    uint16_t caps[3] = {kUiNone, kUiNone, kUiNone};
    auto square = [&](uint16_t id, UiIcon icon) {
        if (id < s.count) {
            uiSetIcon(s, id, icon);
            uiBind(s, id, propVecClick, &s);
        }
        return id;
    };
    if (full) {
        out.lock = square(uiTemplateSlot(s, tpl, row, "lock"), UiIcon::LockOpen);
    }
    for (int i = 0; i < n; ++i) {
        const uint16_t cap = uiTemplateSlot(s, tpl, row, kCap[i]);
        caps[i] = cap;
        uiText(s, cap, " ");
        if (full && cap < s.count) {
            uiBind(s, cap, propVecClick, &s);
        }
        *ids[i] = uiNumber(s, uiTemplateSlot(s, tpl, row, kHold[i]), axes[i], minV, maxV, step);
        uiFlexLimit(s, *ids[i], m.fieldW, m.slotH, 1.0f);
        propFillWidth(s, *ids[i]);
        uiNumberFill(s, *ids[i], false);
        // The field closes the pill: square on the cap side, rounded on the outer end.
        uiSetCap(s, *ids[i], UiCap::Right, "vector_field");
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
    out.reset = square(uiSpawnTemplate(s, tail < s.count ? tail : row, "core/vec_square", "vector_reset"), UiIcon::Reset);
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
