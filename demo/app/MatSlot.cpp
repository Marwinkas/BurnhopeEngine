#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"

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
    const uint16_t row = value < s.count ? s.parentOf[value] : kUiNone;
    if (row < s.count) {
        if (auto* flex = s.ent[row].try_get_mut<UiFlex>()) {
            flex->heightMode = UiSize::Px;
            flex->height = 72.0f;
        }
    }
    if (value < s.count) {
        if (auto* flex = s.ent[value].try_get_mut<UiFlex>()) {
            flex->heightMode = UiSize::Px;
            flex->height = 64.0f;
            flex->gap = 8.0f;
            flex->overflow = UiOverflow::Visible;
        }
    }
}

uint16_t colorChip(UiState& s, HudContext& hud, uint16_t parent, uint16_t index) {
    UiFlex flex{};
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = 22.0f;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.r = 1.0f;
    paint.g = 1.0f;
    paint.b = 1.0f;
    paint.a = 1.0f;
    paint.radius = s.look.radiusSm;
    paint.borderW = 1.0f;
    const uint16_t id = uiPanel(s, parent, flex, paint);
    if (id >= s.count) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    uiBind(s, id, onHudSwatch, &hud);
    hud.bind.rows.push_back(UiBind{.id = id, .index = index, .kind = kBindSwatch, .fallback = 1.0f});
    return id;
}

void addThumb(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    UiFlex frameFlex{};
    frameFlex.widthMode = UiSize::Px;
    frameFlex.heightMode = UiSize::Px;
    frameFlex.width = kThumb;
    frameFlex.height = kThumb;
    frameFlex.shrink = 0.0f;
    frameFlex.overflow = UiOverflow::Hidden;
    frameFlex.align = UiAlign::Center;
    frameFlex.justify = UiJustify::Center;
    const uint16_t frame = uiPanel(s, parent, frameFlex, paintToken(s.look, UiToken::Field, 4.0f));
    hud.matHit[slot].hud = &hud;
    hud.matHit[slot].kind = static_cast<uint8_t>(spec.channel + 1u);
    hud.matTex[slot] = frame;
    if (frame < s.count) {
        s.ent[frame].add<UiFocusable>();
        uiBind(s, frame, onMaterialAct, &hud.matHit[slot]);
        s.ent[frame].set<UiAssetSlot>({frame, matOnPath, &hud.matHit[slot]});
        uiDragAddHost(s, frame);
    }
    hud.matEmpty[slot] = uiLabel(s, frame, "None");
    if (hud.matEmpty[slot] < s.count) {
        s.ent[hud.matEmpty[slot]].add<UiPass>();
        uiTextStyle(s, hud.matEmpty[slot], UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    }
    hud.matThumb[slot] = uiIcon(s, frame, 9, kThumb - 8.0f, kThumb - 8.0f);
    if (hud.matThumb[slot] < s.count) {
        s.ent[hud.matThumb[slot]].add<UiPass>();
        if (auto* image = s.ent[hud.matThumb[slot]].try_get_mut<UiImage>()) {
            image->viewW = 0.25f;
            image->viewH = 1.0f / 3.0f;
            image->viewX = static_cast<float>(spec.cell % 4u) * 0.25f;
            image->viewY = static_cast<float>(spec.cell / 4u) * (1.0f / 3.0f);
        }
        uiShow(s, hud.matThumb[slot], false);
    }
    hud.matClear[slot] = uiButton(s, frame, {.label = "x", .width = 18.0f, .height = 18.0f, .onClick = matOnClear, .user = &hud.matHit[slot]});
    if (hud.matClear[slot] < s.count) {
        uiPlaceAbsolute(s, hud.matClear[slot], kThumb - 22.0f, 2.0f);
        if (auto* paint = s.ent[hud.matClear[slot]].try_get_mut<UiPaint>()) {
            *paint = paintToken(s.look, UiToken::Close, 2.0f);
        }
        uiShow(s, hud.matClear[slot], false);
    }
}

void addSlide(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    hud.matLink[slot].hud = &hud;
    hud.matLink[slot].index = spec.bind;
    const uint16_t slide = uiSlider(s, parent, spec.def, spec.lo, spec.hi, matOnSlide, &hud.matLink[slot], 0.0f);
    hud.matLink[slot].slider = slide;
    if (slide < s.count) {
        if (auto* flex = s.ent[slide].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Auto;
            flex->width = 0.0f;
            flex->grow = 1.0f;
            flex->shrink = 1.0f;
            flex->minW = 48.0f;
        }
    }
    const uint16_t number = uiNumber(s, parent, spec.def, spec.lo, spec.hi, 0.01f, matOnNumber, &hud.matLink[slot], 84.0f, 24.0f, 0.0f);
    hud.matLink[slot].number = number;
    uiNumberFill(s, number, false);
    hud.bind.watch(number, spec.bind, spec.def);
    matResetBtn(s, hud, parent, spec.bind, spec.def, 0.0f, 0);
}

void addColor(UiState& s, HudContext& hud, uint16_t parent, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    UiFlex col{};
    col.direction = UiFlexDir::Column;
    col.justify = UiJustify::Between;
    col.grow = 1.0f;
    col.shrink = 1.0f;
    col.heightMode = UiSize::Px;
    col.height = kThumb;
    col.minW = 96.0f;
    const uint16_t column = uiPanel(s, parent, col, paintClear());
    const uint16_t chip = colorChip(s, hud, column, spec.bind);
    const uint16_t field = uiField(s, column, "#FFFFFF", 0, false);
    const uint8_t hexAt = spec.bind == kScMatEm0 ? 1u : 0u;
    hud.matHex[hexAt] = field;
    if (field < s.count) {
        if (auto* flex = s.ent[field].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Percent;
            flex->width = 100.0f;
            flex->heightMode = UiSize::Px;
            flex->height = 24.0f;
            flex->shrink = 0.0f;
        }
        s.ent[field].set<MatHex>({&hud, spec.bind, chip});
        uiAfterTextAdd(s, field, matHexEdited);
    }
    matResetBtn(s, hud, parent, spec.bind, 1.0f, 0.0f, 3);
}

void showIf(UiState& s, uint16_t id, bool on) {
    if (id == 0 || id >= s.count) {
        return;
    }
    uiShow(s, id, on);
}

void rowHeight(UiState& s, uint16_t id, float h) {
    if (id == 0 || id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr || flex->height == h) {
        return;
    }
    flex->heightMode = UiSize::Px;
    flex->height = h;
    s.layoutDirty = true;
}

void paintHex(UiState& s, HudContext& hud, uint16_t id, uint16_t index, bool withA) {
    if (id == 0 || id >= s.count || s.focused == id) {
        return;
    }
    const UiField* field = s.ent[id].try_get<UiField>();
    if (field == nullptr || index + 2u >= hud.bind.scalarN) {
        return;
    }
    char next[16]{};
    matHexText(next, sizeof(next), hud.block.scalars[index], hud.block.scalars[index + 1u], hud.block.scalars[index + 2u],
        withA ? hud.block.scalars[kScMatOpacity] : 1.0f, withA && hud.block.scalars[kScMatOpacity] < 0.999f);
    if (std::strcmp(field->bytes, next) == 0) {
        return;
    }
    uiSetFieldText(s, id, next);
}

} // namespace

void matAddSlot(UiState& s, HudContext& hud, uint16_t panel, uint8_t slot) {
    const Slot& spec = kSlot[slot];
    const uint16_t value = uiPropRow(s, panel, spec.label);
    hud.matRow[slot] = value < s.count ? s.parentOf[value] : kUiNone;
    lift(s, value);
    addThumb(s, hud, value, slot);
    if (spec.kind == 0) {
        addColor(s, hud, value, slot);
    } else if (spec.kind == 1) {
        addSlide(s, hud, value, slot);
    } else {
        matResetBtn(s, hud, value, spec.channel, 0.0f, 0.0f, 6);
    }
    if (spec.group == 2 && hud.matRow[slot] != kUiNone) {
        uiShow(s, hud.matRow[slot], false);
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
            showIf(*state, hud.matRow[i], pack);
        }
        if (kSlot[i].group == 1) {
            rowHeight(*state, hud.matRow[i], pack ? 32.0f : 72.0f);
            if (hud.matTex[i] != 0 && hud.matTex[i] < state->count) {
                rowHeight(*state, state->parentOf[hud.matTex[i]], pack ? 24.0f : 64.0f);
            }
        }
        const char* path = hudDocPath(hud.matDoc, kSlot[i].channel);
        const bool face = kSlot[i].group != 1 || !pack;
        const bool tex = face && path != nullptr && path[0] != '\0';
        showIf(*state, hud.matThumb[i], tex);
        showIf(*state, hud.matClear[i], tex);
        showIf(*state, hud.matEmpty[i], face && !tex);
        if (hud.matLink[i].slider != 0 && hud.matLink[i].slider != kUiNone && state->capture != hud.matLink[i].slider
            && kSlot[i].bind < hud.bind.scalarN) {
            uiRangeSet(*state, hud.matLink[i].slider, hud.block.scalars[kSlot[i].bind]);
        }
    }
    matSetChoice(*state, hud.matType, hud.block.flags[kFlMatUnlit] != 0 ? 1u : 0u, hud.block.flags[kFlMatUnlit] != 0 ? "Unlit" : "Lit");
    matSetChoice(*state, hud.matPack, pack ? 1u : 0u, pack ? "ORMH" : "Classic");
    paintHex(*state, hud, hud.matHex[0], kScMatBase0, true);
    paintHex(*state, hud, hud.matHex[1], kScMatEm0, false);
    bool dirty = false;
    for (const UiBind& row : hud.bind.rows) {
        if (row.kind != kBindSwatch || row.id >= state->count || row.index + 2u >= hud.bind.scalarN) {
            continue;
        }
        if (row.index != kScMatBase0 && row.index != kScMatEm0) {
            continue;
        }
        auto* paint = state->ent[row.id].try_get_mut<UiPaint>();
        if (paint == nullptr) {
            continue;
        }
        const float r = hud.block.scalars[row.index];
        const float g = hud.block.scalars[row.index + 1u];
        const float b = hud.block.scalars[row.index + 2u];
        if (paint->r == r && paint->g == g && paint->b == b && paint->token == UiToken::Custom) {
            continue;
        }
        paint->r = r;
        paint->g = g;
        paint->b = b;
        paint->token = UiToken::Custom;
        dirty = true;
    }
    if (dirty) {
        uiNotifyVisualDirty(*state);
    }
}

} // namespace burnhope
