#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "color/Color.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/State.hpp"

#include <cstring>

namespace burnhope {

namespace {

constexpr float kThumb = 64.0f;

struct Slot {
    const char* label;
    uint8_t channel;
    uint8_t cell;
    uint8_t group;
    uint8_t kind;
    uint16_t bind;
    float def;
    float lo;
    float hi;
};

constexpr Slot kSlot[kMatSlotN] = {
    {"Albedo", 0, 0, 0, 0, kScMatBase0, 1.0f, 0.0f, 1.0f},
    {"Alpha", 8, 8, 0, 1, kScMatOpacity, 1.0f, 0.0f, 1.0f},
    {"Normal", 1, 3, 0, 1, kScMatNormal, 1.0f, 0.0f, 5.0f},
    {"ORMH", 2, 6, 2, 2, 0, 0.0f, 0.0f, 0.0f},
    {"Metallic", 5, 1, 1, 1, kScMatMetal, 0.0f, 0.0f, 1.0f},
    {"Roughness", 4, 2, 1, 1, kScMatRough, 0.5f, 0.0f, 1.0f},
    {"Ambient Occlusion", 6, 5, 1, 1, kScMatAo, 1.0f, 0.0f, 1.0f},
    {"Height", 7, 4, 1, 1, kScMatHeight, 0.0f, 0.0f, 4.0f},
    {"Emission", 3, 7, 0, 0, kScMatEm0, 1.0f, 0.0f, 1.0f},
};

void lift(UiState& s, uint16_t value) {
    uiPropRowHeight(s, value, 72.0f, 64.0f);
    uiSetGap(s, value, 8.0f);
}

void addThumb(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot) {
    const Slot& spec = kSlot[slot];
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

void addSlide(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    hud.bind.sliderNumber(s, parent, spec.bind, spec.def, spec.lo, spec.hi, 0.01f);
    matResetBtn(s, hud, parent, spec.bind, spec.def, 0.0f, 0);
}

void addColor(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    hud.bind.colorHex(s, parent, spec.bind, kScMatOpacity, spec.bind == kScMatBase0, &hud.matEdited);
    matResetBtn(s, hud, parent, spec.bind, 1.0f, 0.0f, 3);
}

void showIf(UiState& s, uint16_t id, bool on) {
    if (id == 0 || id >= s.count) {
        return;
    }
    uiShow(s, id, on);
}

} // namespace

void matAddSlot(UiState& s, HudContext& hud, uint16_t panel, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    const uint16_t value = uiPropRow(s, panel, spec.label);
    hud.matVal[slot] = value;
    lift(s, value);
    addThumb(s, hud, value, slot);
    if (spec.kind == 0) {
        addColor(s, hud, value, slot);
    } else if (spec.kind == 1) {
        addSlide(s, hud, value, slot);
    } else {
        matResetBtn(s, hud, value, spec.channel, 0.0f, 0.0f, 6);
    }
    if (spec.group == 2) {
        uiPropRowShow(s, value, false);
    }
}

void hudPaintMaterial(HudContext& hud) {
    UiState* state = matPageState(hud);
    if (state == nullptr) {
        return;
    }
    const bool pack = hud.block.flags[kFlMatPack] != 0;
    for (uint8_t i = 0; i < kMatSlotN; ++i) {
        if (kSlot[i].group == 2) {
            uiPropRowShow(*state, hud.matVal[i], pack);
        }
        if (kSlot[i].group == 1) {
            uiPropRowHeight(*state, hud.matVal[i], pack ? 32.0f : 72.0f, pack ? 24.0f : 64.0f);
        }
        const char* path = hudDocPath(hud.matDoc, kSlot[i].channel);
        const bool face = kSlot[i].group != 1 || !pack;
        const bool tex = face && path != nullptr && path[0] != '\0';
        showIf(*state, hud.matThumb[i], tex);
        showIf(*state, hud.matClear[i], tex);
        showIf(*state, hud.matEmpty[i], face && !tex);
    }
    uiChoiceSelect(*state, hud.matType, hud.block.flags[kFlMatUnlit] != 0 ? 1u : 0u, hud.block.flags[kFlMatUnlit] != 0 ? "Unlit" : "Lit");
    uiChoiceSelect(*state, hud.matPack, pack ? 1u : 0u, pack ? "ORMH" : "Classic");
}

} // namespace burnhope
