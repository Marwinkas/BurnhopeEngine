#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "host/PropGrid.hpp"
#include "host/PropImport.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/Style.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Label.hpp"
#include "ui/Nodes.hpp"

namespace burnhope {

namespace {

constexpr float kVecRange = 64.0f; // data range of a Tiling / Offset field, not a style

void onType(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    UiState* state = hud != nullptr ? matPageState(*hud) : nullptr;
    if (state != nullptr) {
        matApplyFlag(*hud, *state, kFlMatUnlit, index == 1 ? 1u : 0u);
        saveOpenMaterial(*hud);
    }
}

void onPack(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    UiState* state = hud != nullptr ? matPageState(*hud) : nullptr;
    if (state != nullptr) {
        matApplyFlag(*hud, *state, kFlMatPack, index == 0 ? 0u : 1u);
        hudPaintMaterial(*hud);
        saveOpenMaterial(*hud);
    }
}

uint16_t rowOf(UiState& s, uint16_t parent, float gap, UiJustify justify) {
    UiFlex flex{};
    flex.direction = UiFlexDir::Row;
    flex.align = UiAlign::Center;
    flex.justify = justify;
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = uiStyle(s, "mat_row", UiStyleKey::Height);
    flex.shrink = 0.0f;
    flex.gap = gap;
    return uiPanel(s, parent, flex, paintClear());
}

uint16_t captionOf(UiState& s, uint16_t row, const char* text) {
    const uint16_t id = uiLabel(s, row, text);
    const float labelW = uiStyle(s, "mat_row", UiStyleKey::LabelW);
    uiSetWidth(s, id, labelW);
    uiSetGrow(s, id, 0.0f, 0.0f, labelW);
    return id;
}

uint16_t badgeOf(UiState& s, uint16_t row, const char* letter, uint8_t axis) {
    UiFlex flex{};
    flex.widthMode = UiSize::Px;
    flex.width = uiStyle(s, "mat_badge", UiStyleKey::Width);
    flex.heightMode = UiSize::Px;
    flex.height = uiStyle(s, "mat_badge", UiStyleKey::Height);
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.r = kUiAxis[axis][0];
    paint.g = kUiAxis[axis][1];
    paint.b = kUiAxis[axis][2];
    paint.a = 1.0f;
    paint.radius = uiStyle(s, "mat_badge", UiStyleKey::Radius);
    const uint16_t id = uiPanel(s, row, flex, paint);
    uiText(s, id, letter);
    uiTextStyle(s, id, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiSetPass(s, id);
    return id;
}

} // namespace

// A property row: the label in a fixed-width column, kCheckGap of clear space, then the check box.
uint16_t matFlagCheck(UiState& s, UiPropBinder& bind, uint16_t parent, const char* label, float labelW, uint16_t flag, bool on) {
    if (bind.flags != nullptr && flag < bind.flagN) {
        bind.flags[flag] = on ? 1u : 0u;
    }
    const float checkW = uiStyle(s, "mat_check", UiStyleKey::Width);
    const uint16_t row = rowOf(s, parent, uiStyle(s, "mat_check", UiStyleKey::Gap), UiJustify::Start);
    const uint16_t caption = uiLabel(s, row, label);
    uiSetWidth(s, caption, labelW);
    uiSetGrow(s, caption, 0.0f, 0.0f, labelW);
    const uint16_t id = uiCheck(s, row, "", 0, 0, on);
    uiSetWidth(s, id, checkW);
    uiSetGrow(s, id, 0.0f, 0.0f, checkW);
    bind.rows.push_back(UiBind{.id = id, .index = flag, .kind = kBindFlag, .fallback = on ? 1.0f : 0.0f});
    return id;
}

uint16_t matNumber(UiState& s, UiPropBinder& bind, uint16_t parent, uint16_t scalar, float def, float lo, float hi, float step, float width) {
    const uint16_t id = uiNumber(s, parent, def, lo, hi, step);
    uiFlexLimit(s, id, width, uiStyle(s, "mat_row", UiStyleKey::Inner), 0.0f);
    uiNumberFill(s, id, false);
    bind.watch(id, scalar, def);
    return id;
}

// PropRole "matrow": one compact row of the material page's left column. kind 0 is a flag check, 1 a Tiling / Offset
// pair of number fields with a reset, 2 a labelled choice (Type or Maps).
uint16_t matRowRole(void* user, UiState& s, UiPropBinder& bind, uint16_t panel, const PropSpec& spec) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return kUiNone;
    }
    if (spec.kind == 0) {
        const float labelW = uiStyle(s, "mat_row", UiStyleKey::LabelW) + uiStyle(s, "mat_row", UiStyleKey::Gap) - uiStyle(s, "mat_check", UiStyleKey::Gap);
        return matFlagCheck(s, bind, panel, spec.label, labelW, spec.bind, spec.value >= 0.5f);
    }
    const uint16_t row = rowOf(s, panel, uiStyle(s, "mat_row", UiStyleKey::Gap), UiJustify::Start);
    (void)captionOf(s, row, spec.label);
    if (spec.kind == 1) {
        for (uint16_t axis = 0; axis < 2u; ++axis) {
            (void)badgeOf(s, row, axis == 0 ? "X" : "Y", static_cast<uint8_t>(axis));
            (void)matNumber(s, bind, row, static_cast<uint16_t>(spec.bind + axis), spec.value, -kVecRange, kVecRange, 0.01f, uiStyle(s, "mat_row", UiStyleKey::FieldW));
        }
        const uint16_t reset = bind.resetBtn(s, row, UiReset::Vec2, spec.bind, spec.value, spec.value);
        const float resetSide = uiStyle(s, "mat_row", UiStyleKey::Trail);
        uiSetSize(s, reset, resetSide, resetSide);
        return row;
    }
    const bool type = spec.bind == 56;
    static constexpr const char* kTypeNames[] = {"Lit", "Unlit"};
    static constexpr const char* kPackNames[] = {"Classic", "Packed ORM"};
    const char* const* names = type ? kTypeNames : kPackNames;
    const float pickW = uiStyle(s, "mat_row", UiStyleKey::Width);
    const uint16_t pick = uiChoice(s, row, {
        .labels = names,
        .count = 2,
        .selected = 0,
        .onPick = type ? onType : onPack,
        .user = hud,
    });
    uiChoiceFit(s, pick, pickW);
    (type ? hud->matType : hud->matPack) = pick;
    return row;
}

} // namespace burnhope
