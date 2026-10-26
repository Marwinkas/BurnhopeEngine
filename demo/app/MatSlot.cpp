#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "color/Color.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Text.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "host/PropGrid.hpp"
#include "host/PropImport.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/Nodes.hpp"

#include <cstring>

namespace burnhope {

namespace {

constexpr float kThumb = 64.0f;

void lift(UiState& s, uint16_t value) {
    uiPropRowHeight(s, value, uiStyle(s, "prop_row_tall", UiStyleKey::Height, 74.0f), uiStyle(s, "prop_row_tall", UiStyleKey::Inner, 64.0f));
    uiPropValueMax(s, value, uiStyle(s, "prop_row_tall", UiStyleKey::MaxW, 340.0f));
    uiSetGap(s, value, 8.0f);
}

void addThumb(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot, const PropSpec& spec) {
    hud.matHit[slot].hud = &hud;
    hud.matHit[slot].kind = static_cast<uint8_t>(spec.channel + 1u);
    const UiTexSlotIds ids = uiTexSlot(s, parent, {
        .side = kThumb,
        .photo = 9,
        .atlasCols = 4,
        .atlasRows = 3,
        .cell = spec.cell,
        .onBrowse = onMaterialAct,
        .onClear = matOnClear,
        .onPath = matOnPath,
        .user = &hud.matHit[slot],
    });
    hud.matTex[slot] = ids.frame;
    hud.matEmpty[slot] = ids.empty;
    hud.matThumb[slot] = ids.thumb;
    hud.matClear[slot] = ids.clear;
}

void addSlide(UiState& s, HudContext& hud, uint16_t parent, const PropSpec& spec) {
    hud.bind.sliderNumber(s, parent, spec.bind, spec.value, spec.minV, spec.maxV, 0.01f);
    (void)hud.bind.resetBtn(s, parent, UiReset::Float, spec.bind, spec.value);
}

void addColor(UiState& s, HudContext& hud, uint16_t parent, const PropSpec& spec) {
    hud.bind.colorHex(s, parent, spec.bind, kScMatOpacity, spec.bind == kScMatBase0, &hud.matEdited);
    (void)hud.bind.resetBtn(s, parent, UiReset::Rgb, spec.bind, 1.0f);
}

void showIf(UiState& s, uint16_t id, bool on) {
    if (id == 0) {
        return;
    }
    uiShow(s, id, on);
}

} // namespace

uint16_t matSlotRole(void* user, UiState& s, UiPropBinder&, uint16_t panel, const PropSpec& spec) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || spec.slot >= kMatSlotN) {
        return kUiNone;
    }
    const uint8_t slot = spec.slot;
    const uint16_t value = uiPropRow(s, panel, spec.label);
    hud->matVal[slot] = value;
    hud->matGroup[slot] = spec.group;
    hud->matChannel[slot] = spec.channel;
    lift(s, value);
    addThumb(s, *hud, value, slot, spec);
    if (spec.kind == 0) {
        addColor(s, *hud, value, spec);
    } else if (spec.kind == 1) {
        addSlide(s, *hud, value, spec);
    } else {
        (void)hud->bind.resetBtn(s, value, UiReset::Custom, spec.channel, 0.0f);
    }
    if (spec.group == 2) {
        uiPropRowShow(s, value, false);
    }
    return value;
}

void hudPaintMaterial(HudContext& hud) {
    UiState* state = matPageState(hud);
    if (state == nullptr) {
        return;
    }
    const bool pack = hud.block.flags[kFlMatPack] != 0;
    for (uint8_t i = 0; i < kMatSlotN; ++i) {
        if (hud.matGroup[i] == 2) {
            uiPropRowShow(*state, hud.matVal[i], pack);
        }
        if (hud.matGroup[i] == 1) {
            uiPropRowHeight(*state, hud.matVal[i], pack ? uiStyle(*state, "prop_row", UiStyleKey::Height, 34.0f) : uiStyle(*state, "prop_row_tall", UiStyleKey::Height, 74.0f), pack ? uiStyle(*state, "prop_row", UiStyleKey::Inner, 26.0f) : uiStyle(*state, "prop_row_tall", UiStyleKey::Inner, 64.0f));
        }
        const char* path = hudDocPath(hud.matDoc, hud.matChannel[i]);
        const bool face = hud.matGroup[i] != 1 || !pack;
        const bool tex = face && path != nullptr && path[0] != '\0';
        showIf(*state, hud.matThumb[i], tex);
        showIf(*state, hud.matClear[i], tex);
        showIf(*state, hud.matEmpty[i], face && !tex);
    }
    uiChoiceSelect(*state, hud.matType, hud.block.flags[kFlMatUnlit] != 0 ? 1u : 0u, hud.block.flags[kFlMatUnlit] != 0 ? "Unlit" : "Lit");
    uiChoiceSelect(*state, hud.matPack, pack ? 1u : 0u, pack ? "ORMH" : "Classic");
}

} // namespace burnhope
