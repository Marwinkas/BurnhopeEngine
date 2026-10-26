#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "host/PropGrid.hpp"
#include "host/PropImport.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Number.hpp"
#include "ui/Nodes.hpp"

namespace burnhope {

namespace {

constexpr float kRowH = 28.0f;
constexpr float kLabelW = 60.0f;
constexpr float kFieldW = 64.0f;
constexpr float kFieldH = 26.0f;
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

uint16_t rowOf(UiState& s, uint16_t parent) {
    UiFlex flex{};
    flex.direction = UiFlexDir::Row;
    flex.align = UiAlign::Center;
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = kRowH;
    flex.shrink = 0.0f;
    flex.gap = 6.0f;
    return uiPanel(s, parent, flex, paintClear());
}

uint16_t captionOf(UiState& s, uint16_t row, const char* text) {
    const uint16_t id = uiLabel(s, row, text);
    uiSetWidth(s, id, kLabelW);
    uiSetGrow(s, id, 0.0f, 0.0f, kLabelW);
    return id;
}

} // namespace

uint16_t matFlagCheck(UiState& s, UiPropBinder& bind, uint16_t parent, const char* label, uint16_t flag, bool on) {
    if (bind.flags != nullptr && flag < bind.flagN) {
        bind.flags[flag] = on ? 1u : 0u;
    }
    const uint16_t id = uiCheck(s, parent, label, 0, 0, on);
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
    const uint16_t row = rowOf(s, panel);
    if (spec.kind == 0) {
        (void)matFlagCheck(s, bind, row, spec.label, spec.bind, spec.value >= 0.5f);
        (void)bind.resetBtn(s, row, UiReset::Flag, spec.bind, spec.value >= 0.5f ? 1.0f : 0.0f);
        return row;
    }
    (void)captionOf(s, row, spec.label);
    if (spec.kind == 1) {
        for (uint16_t axis = 0; axis < 2u; ++axis) {
            (void)matNumber(s, bind, row, static_cast<uint16_t>(spec.bind + axis), spec.value, -kVecRange, kVecRange, 0.01f);
        }
        (void)bind.resetBtn(s, row, UiReset::Vec2, spec.bind, spec.value, spec.value);
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
