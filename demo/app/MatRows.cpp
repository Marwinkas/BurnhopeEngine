#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "host/PropGrid.hpp"
#include "host/PropImport.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/Style.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Label.hpp"
#include "ui/Nodes.hpp"

namespace burnhope {

namespace {

constexpr float kRowH = 26.0f;
constexpr float kLabelW = 46.0f;
constexpr float kFieldW = 54.0f;
constexpr float kFieldH = 24.0f;
constexpr float kBadgeW = 16.0f;
constexpr float kBadgeH = 20.0f;
constexpr float kCheckW = 36.0f;
constexpr float kResetSide = 22.0f;
constexpr float kVecRange = 64.0f;

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
    flex.height = kRowH;
    flex.shrink = 0.0f;
    flex.gap = gap;
    return uiPanel(s, parent, flex, paintClear());
}

uint16_t captionOf(UiState& s, uint16_t row, const char* text) {
    const uint16_t id = uiLabel(s, row, text);
    uiSetWidth(s, id, kLabelW);
    uiSetGrow(s, id, 0.0f, 0.0f, kLabelW);
    return id;
}

uint16_t badgeOf(UiState& s, uint16_t row, const char* letter, uint8_t axis) {
    UiFlex flex{};
    flex.widthMode = UiSize::Px;
    flex.width = kBadgeW;
    flex.heightMode = UiSize::Px;
    flex.height = kBadgeH;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.r = kUiAxis[axis][0];
    paint.g = kUiAxis[axis][1];
    paint.b = kUiAxis[axis][2];
    paint.a = 1.0f;
    paint.radius = 2.0f;
    const uint16_t id = uiPanel(s, row, flex, paint);
    uiText(s, id, letter);
    uiTextStyle(s, id, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiSetPass(s, id);
    return id;
}

} // namespace

// A property row: the label on the left, the check box on the right.
uint16_t matFlagCheck(UiState& s, UiPropBinder& bind, uint16_t parent, const char* label, uint16_t flag, bool on) {
    if (bind.flags != nullptr && flag < bind.flagN) {
        bind.flags[flag] = on ? 1u : 0u;
    }
    const uint16_t row = rowOf(s, parent, 6.0f, UiJustify::Between);
    const uint16_t caption = uiLabel(s, row, label);
    uiSetGrow(s, caption, 1.0f, 1.0f, 0.0f);
    const uint16_t id = uiCheck(s, row, "", 0, 0, on);
    uiSetWidth(s, id, kCheckW);
    uiSetGrow(s, id, 0.0f, 0.0f, kCheckW);
    bind.rows.push_back(UiBind{.id = id, .index = flag, .kind = kBindFlag, .fallback = on ? 1.0f : 0.0f});
    return id;
}

uint16_t matNumber(UiState& s, UiPropBinder& bind, uint16_t parent, uint16_t scalar, float def, float lo, float hi, float step) {
    const uint16_t id = uiNumber(s, parent, def, lo, hi, step, nullptr, nullptr, kFieldW, kFieldH, 0.0f);
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
        return matFlagCheck(s, bind, panel, spec.label, spec.bind, spec.value >= 0.5f);
    }
    const uint16_t row = rowOf(s, panel, 2.0f, UiJustify::Start);
    (void)captionOf(s, row, spec.label);
    if (spec.kind == 1) {
        for (uint16_t axis = 0; axis < 2u; ++axis) {
            (void)badgeOf(s, row, axis == 0 ? "X" : "Y", static_cast<uint8_t>(axis));
            (void)matNumber(s, bind, row, static_cast<uint16_t>(spec.bind + axis), spec.value, -kVecRange, kVecRange, 0.01f);
        }
        const uint16_t reset = bind.resetBtn(s, row, UiReset::Vec2, spec.bind, spec.value, spec.value);
        uiSetSize(s, reset, kResetSide, kResetSide);
        return row;
    }
    const bool type = spec.bind == 56;
    static constexpr const char* kTypeNames[] = {"Lit", "Unlit"};
    static constexpr const char* kPackNames[] = {"Classic", "Packed ORM"};
    const char* const* names = type ? kTypeNames : kPackNames;
    const uint16_t pick = uiChoice(s, row, {
        .labels = names,
        .count = 2,
        .selected = 0,
        .width = uiChoiceWidth(s, names, 2),
        .onPick = type ? onType : onPack,
        .user = hud,
    });
    uiChoiceFit(s, pick, uiChoiceWidth(s, names, 2));
    (type ? hud->matType : hud->matPack) = pick;
    return row;
}

} // namespace burnhope
