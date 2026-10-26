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
#include "ui/State.hpp"

#include <cstring>

namespace burnhope {

namespace {

constexpr float kThumb = 52.0f;
constexpr float kCardGap = 8.0f;
constexpr float kLineH = 22.0f;
constexpr float kEmissionMax = 16.0f;
constexpr uint8_t kChannelEmission = 3;
constexpr uint8_t kChannelHeight = 7;

UiFlex lineFlex(float height) {
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.widthMode = UiSize::Percent;
    line.width = 100.0f;
    line.heightMode = UiSize::Px;
    line.height = height;
    line.shrink = 0.0f;
    line.gap = 6.0f;
    return line;
}

uint16_t addThumb(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot, const PropSpec& spec) {
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
    return ids.frame;
}

void addSlide(UiState& s, HudContext& hud, uint16_t parent, uint16_t bind, float value, float lo, float hi) {
    hud.bind.sliderNumber(s, parent, bind, value, lo, hi, 0.01f);
    (void)hud.bind.resetBtn(s, parent, UiReset::Float, bind, value);
}

void addColor(UiState& s, HudContext& hud, uint16_t parent, const PropSpec& spec) {
    hud.bind.colorHex(s, parent, spec.bind, kScMatOpacity, spec.bind == kScMatBase0, &hud.matEdited);
    (void)hud.bind.resetBtn(s, parent, UiReset::Rgb, spec.bind, 1.0f);
}

void showIf(UiState& s, uint16_t id, bool on) {
    if (id == 0 || id == kUiNone) {
        return;
    }
    uiShow(s, id, on);
}

uint16_t addCaption(UiState& s, uint16_t parent, const char* text, float grow) {
    UiFlex flex{};
    flex.heightMode = UiSize::Px;
    flex.height = kLineH;
    flex.grow = grow;
    flex.shrink = 1.0f;
    flex.overflow = UiOverflow::Hidden;
    const uint16_t id = uiPanel(s, parent, flex, paintLabel(s.look));
    uiText(s, id, text);
    uiTextStyle(s, id, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    return id;
}

} // namespace

// One slot card: [thumbnail] [label ........ X] / [bound control], so the red clear button sits on the label row's far edge.
uint16_t matSlotRole(void* user, UiState& s, UiPropBinder&, uint16_t panel, const PropSpec& spec) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || spec.slot >= kMatSlotN) {
        return kUiNone;
    }
    const uint8_t slot = spec.slot;
    UiFlex cardFlex = lineFlex(kThumb + 4.0f);
    cardFlex.align = UiAlign::Start;
    cardFlex.gap = kCardGap;
    cardFlex.padT = 2.0f;
    cardFlex.padB = 2.0f;
    const uint16_t card = uiPanel(s, panel, cardFlex, paintClear());
    hud->matVal[slot] = card;
    hud->matGroup[slot] = spec.group;
    hud->matChannel[slot] = spec.channel;
    (void)addThumb(s, *hud, card, slot, spec);
    UiFlex colFlex{};
    colFlex.direction = UiFlexDir::Column;
    colFlex.grow = 1.0f;
    colFlex.shrink = 1.0f;
    colFlex.gap = 4.0f;
    colFlex.minW = 40.0f;
    const uint16_t col = uiPanel(s, card, colFlex, paintClear());
    const uint16_t top = uiPanel(s, col, lineFlex(kLineH), paintClear());
    (void)addCaption(s, top, spec.label, 1.0f);
    const uint16_t clear = hud->matClear[slot];
    if (clear < s.count) {
        uiReparent(s, clear, top);
        if (auto* flex = s.ent[clear].try_get_mut<UiFlex>()) {
            flex->position = UiPosition::Relative;
            flex->posX = 0.0f;
            flex->posY = 0.0f;
            flex->shrink = 0.0f;
        }
    }
    UiFlex ctlFlex = lineFlex(kLineH + 2.0f);
    const uint16_t line = uiPanel(s, col, ctlFlex, paintClear());
    UiFlex boxFlex = lineFlex(kLineH + 2.0f);
    boxFlex.grow = 1.0f;
    boxFlex.widthMode = UiSize::Auto;
    const uint16_t box = uiPanel(s, line, boxFlex, paintClear());
    hud->matCtl[slot] = box;
    hud->matDriven[slot] = 0;
    if (spec.kind == 0) {
        addColor(s, *hud, box, spec);
        if (spec.channel == kChannelEmission) {
            addSlide(s, *hud, box, kScMatEmissiveGain, 0.0f, 0.0f, kEmissionMax);
        }
    } else if (spec.kind == 1) {
        addSlide(s, *hud, box, spec.bind, spec.value, spec.minV, spec.maxV);
        if (spec.group == 1 && spec.channel != kChannelHeight) {
            hud->matDriven[slot] = addCaption(s, line, "Driven by texture map", 1.0f);
            uiShow(s, hud->matDriven[slot], false);
        }
    } else {
        (void)hud->bind.resetBtn(s, box, UiReset::Custom, spec.channel, 0.0f);
    }
    if (spec.group == 2) {
        uiShow(s, card, false);
    }
    return card;
}

void hudPaintMaterial(HudContext& hud) {
    UiState* state = matPageState(hud);
    if (state == nullptr) {
        return;
    }
    const bool pack = hud.block.flags[kFlMatPack] != 0;
    for (uint8_t i = 0; i < kMatSlotN; ++i) {
        if (hud.matGroup[i] == 2) {
            showIf(*state, hud.matVal[i], pack);
        }
        const char* path = hudDocPath(hud.matDoc, hud.matChannel[i]);
        const bool face = hud.matGroup[i] != 1 || !pack;
        const bool tex = face && path != nullptr && path[0] != '\0';
        const bool driven = tex && hud.matDriven[i] != 0;
        showIf(*state, hud.matTex[i], face);
        showIf(*state, hud.matThumb[i], tex);
        showIf(*state, hud.matClear[i], tex);
        showIf(*state, hud.matEmpty[i], face && !tex);
        showIf(*state, hud.matCtl[i], !driven);
        showIf(*state, hud.matDriven[i], driven);
    }
    uiChoiceSelect(*state, hud.matType, hud.block.flags[kFlMatUnlit] != 0 ? 1u : 0u, hud.block.flags[kFlMatUnlit] != 0 ? "Unlit" : "Lit");
    uiChoiceSelect(*state, hud.matPack, pack ? 1u : 0u, pack ? "ORMH" : "Classic");
}

} // namespace burnhope
