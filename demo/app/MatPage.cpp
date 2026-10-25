#include "app/HudBuild.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Slider.hpp"

namespace burnhope {

namespace {

constexpr uint16_t kNoBind = 0xFFFFu;
constexpr float kCell = 148.0f;
constexpr float kThumb = 96.0f;

struct Card {
    const char* title;
    uint8_t channel;
    uint16_t slide;
    float def;
    float lo;
    float hi;
    uint16_t swatch;
};

constexpr Card kCards[8] = {
    {"Albedo", 0, kNoBind, 0.0f, 0.0f, 0.0f, kScMatBase0},
    {"Metallic", 5, kScMatMetal, 0.0f, 0.0f, 1.0f, kNoBind},
    {"Roughness", 4, kScMatRough, 0.5f, 0.0f, 1.0f, kNoBind},
    {"Normal", 1, kScMatNormal, 1.0f, 0.0f, 5.0f, kNoBind},
    {"Height", 7, kScMatHeight, 0.0f, 0.0f, 4.0f, kNoBind},
    {"AO", 6, kScMatAo, 1.0f, 0.0f, 1.0f, kNoBind},
    {"ORM", 2, kNoBind, 0.0f, 0.0f, 0.0f, kNoBind},
    {"Emission", 3, kScMatEmissiveGain, 0.0f, 0.0f, 8.0f, kScMatEm0},
};

void onCardClear(void* user, uint16_t) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->kind < 1) {
        return;
    }
    hudDocPath(hit->hud->matDoc, static_cast<uint8_t>(hit->kind - 1u))[0] = '\0';
    hudCommitMaps(*hit->hud, 255);
    hit->hud->atlasDirty = 1;
    hit->hud->previewDirty = 1;
    hudPaintMaterial(*hit->hud);
    saveOpenMaterial(*hit->hud);
}

void onCardPath(void* user, const char* path) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || path == nullptr || hit->kind < 1) {
        return;
    }
    hudTakeTexture(*hit->hud, static_cast<uint8_t>(hit->kind - 1u), path);
}

void onPreviewDrag(void* user, uint16_t, float x, float y) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    if (hud->previewDown == 0) {
        hud->previewDown = 1;
        hud->previewPx = x;
        hud->previewPy = y;
        return;
    }
    const float dx = x - hud->previewPx;
    const float dy = y - hud->previewPy;
    hud->previewPx = x;
    hud->previewPy = y;
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }
    hud->previewYaw += dx * 0.012f;
    hud->previewPitch += dy * 0.012f;
    if (hud->previewPitch > 1.2f) {
        hud->previewPitch = 1.2f;
    }
    if (hud->previewPitch < -1.2f) {
        hud->previewPitch = -1.2f;
    }
    hud->previewDirty = 1;
}

void onSaveBar(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->matSent = {};
    saveOpenMaterial(*hud);
}

uint16_t colorChip(UiState& s, HudContext& hud, uint16_t parent, uint16_t index) {
    UiFlex flex{};
    flex.widthMode = UiSize::Px;
    flex.heightMode = UiSize::Px;
    flex.width = 36.0f;
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

void addCell(UiState& s, HudContext& hud, uint16_t grid, uint8_t index) {
    const Card& card = kCards[index];
    UiFlex host{};
    host.direction = UiFlexDir::Column;
    host.widthMode = UiSize::Px;
    host.width = kCell;
    host.gap = 4.0f;
    host.shrink = 0.0f;
    const uint16_t box = uiPanel(s, grid, host, paintClear());
    uiLabel(s, box, card.title);
    UiFlex frameFlex{};
    frameFlex.widthMode = UiSize::Px;
    frameFlex.heightMode = UiSize::Px;
    frameFlex.width = kThumb;
    frameFlex.height = kThumb;
    frameFlex.shrink = 0.0f;
    frameFlex.overflow = UiOverflow::Hidden;
    frameFlex.align = UiAlign::Center;
    frameFlex.justify = UiJustify::Center;
    const uint16_t frame = uiPanel(s, box, frameFlex, paintToken(s.look, UiToken::Field, 4.0f));
    hud.matHit[index].hud = &hud;
    hud.matHit[index].kind = static_cast<uint8_t>(card.channel + 1u);
    hud.matTex[index] = frame;
    if (frame < s.count) {
        s.ent[frame].add<UiFocusable>();
        uiBind(s, frame, onMaterialAct, &hud.matHit[index]);
        UiAssetSlot drop{};
        drop.caption = frame;
        drop.onPath = onCardPath;
        drop.user = &hud.matHit[index];
        s.ent[frame].set<UiAssetSlot>(drop);
        uiDragAddHost(s, frame);
    }
    hud.matThumb[index] = uiIcon(s, frame, 9, kThumb - 8.0f, kThumb - 8.0f);
    if (hud.matThumb[index] < s.count) {
        s.ent[hud.matThumb[index]].add<UiPass>();
        if (auto* image = s.ent[hud.matThumb[index]].try_get_mut<UiImage>()) {
            image->viewW = 0.25f;
            image->viewH = 0.5f;
            image->viewX = static_cast<float>(index % 4u) * 0.25f;
            image->viewY = static_cast<float>(index / 4u) * 0.5f;
        }
    }
    hud.matClear[index] = uiButton(s, frame, {.label = "x", .width = 18.0f, .height = 18.0f, .onClick = onCardClear, .user = &hud.matHit[index]});
    if (hud.matClear[index] < s.count) {
        uiPlaceAbsolute(s, hud.matClear[index], kThumb - 22.0f, 4.0f);
        if (auto* paint = s.ent[hud.matClear[index]].try_get_mut<UiPaint>()) {
            *paint = paintToken(s.look, UiToken::Close, 2.0f);
        }
    }
    if (card.swatch != kNoBind) {
        colorChip(s, hud, box, card.swatch);
    }
    if (card.slide != kNoBind) {
        const uint16_t slide = uiSlider(s, box, card.def, card.lo, card.hi, nullptr, nullptr, kCell - 8.0f);
        hud.bind.watch(slide, card.slide, card.def);
    }
}

} // namespace

void hudPaintMaterial(HudContext& hud) {
    UiState* state = hud.tool.canvas != nullptr ? hud.tool.canvas->state.get() : nullptr;
    if (state == nullptr) {
        state = hudUi(hud);
    }
    if (state == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < 8; ++i) {
        uiText(*state, hud.matTex[i], "");
    }
    for (const UiBind& row : hud.bind.rows) {
        if (row.kind != kBindSwatch || row.id >= state->count || row.index + 2u >= 256u) {
            continue;
        }
        if (row.index != kScMatBase0 && row.index != kScMatEm0) {
            continue;
        }
        if (auto* paint = state->ent[row.id].try_get_mut<UiPaint>()) {
            paint->r = hud.block.scalars[row.index];
            paint->g = hud.block.scalars[row.index + 1u];
            paint->b = hud.block.scalars[row.index + 2u];
            paint->token = UiToken::Custom;
        }
    }
    uiNotifyVisualDirty(*state);
}

void hudBuildMaterialPage(Canvas& canvas, HudContext* hud, uint16_t panel) {
    if (canvas.state == nullptr || hud == nullptr || panel == kUiNone) {
        return;
    }
    UiState& s = *canvas.state;
    if (hud->previewBuf.size() != 168u * 168u * 4u) {
        hud->previewBuf.resize(168u * 168u * 4u);
    }
    if (hud->atlasBuf.size() != 256u * 128u * 4u) {
        hud->atlasBuf.resize(256u * 128u * 4u);
    }
    UiFlex previewCol{};
    previewCol.direction = UiFlexDir::Column;
    previewCol.align = UiAlign::Center;
    previewCol.widthMode = UiSize::Percent;
    previewCol.width = 100.0f;
    previewCol.gap = 4.0f;
    previewCol.shrink = 0.0f;
    const uint16_t preview = uiPanel(s, panel, previewCol, paintClear());
    hud->matPreview = uiIcon(s, preview, 8, 168.0f, 168.0f);
    uiDrag(s, hud->matPreview, onPreviewDrag, hud);
    uiLabel(s, preview, "Drag: Rotate");
    uiLabel(s, panel, "Texturing & UV");
    hud->bind.check(s, panel, "Repeat", kFlMatRepeat, true);
    hud->bind.check(s, panel, "Triplanar", kFlMatWorld, false);
    hud->bind.slide(s, panel, "Tiling X", kScMatUvX, 1.0f, 0.05f, 32.0f);
    hud->bind.slide(s, panel, "Tiling Y", kScMatUvY, 1.0f, 0.05f, 32.0f);
    hud->bind.slide(s, panel, "Offset X", kScMatUvOffX, 0.0f, -10.0f, 10.0f);
    hud->bind.slide(s, panel, "Offset Y", kScMatUvOffY, 0.0f, -10.0f, 10.0f);
    hud->bind.slide(s, panel, "Rotation", kScMatUvRot, 0.0f, -180.0f, 180.0f);
    hud->bind.slide(s, panel, "World Grid", kScMatGrid, 1.0f, 0.05f, 25.0f);
    hud->bind.check(s, panel, "Unlit", kFlMatUnlit, false);
    hud->bind.check(s, panel, "Double Sided", kFlMatDouble, false);
    hud->bind.check(s, panel, "Flip Normal Y", kFlMatFlip, true);
    UiFlex grid{};
    grid.direction = UiFlexDir::Row;
    grid.wrap = UiFlexWrap::Wrap;
    grid.gap = 12.0f;
    grid.widthMode = UiSize::Percent;
    grid.width = 100.0f;
    grid.shrink = 0.0f;
    grid.align = UiAlign::Start;
    const uint16_t cells = uiPanel(s, panel, grid, paintClear());
    for (uint8_t i = 0; i < 8; ++i) {
        addCell(s, *hud, cells, i);
    }
    hud->bind.slide(s, panel, "Opacity", kScMatOpacity, 1.0f, 0.0f, 1.0f);
    hud->bind.check(s, panel, "Transparent", kFlMatAlpha, false);
    hud->bind.slide(s, panel, "Alpha Cutoff", kScMatCutoff, 0.5f, 0.0f, 1.0f);
    hud->bind.slide(s, panel, "Reflectance", kScMatReflect, 0.5f, 0.0f, 1.0f);
    hud->bind.slide(s, panel, "Clearcoat", kScMatCoat, 0.0f, 0.0f, 1.0f);
    hud->bind.slide(s, panel, "Clearcoat Roughness", kScMatCoatRough, 0.04f, 0.04f, 1.0f);
    const uint16_t save = uiButton(s, panel, {.label = "Save Material", .height = 32.0f, .fillWidth = true, .onClick = onSaveBar, .user = hud});
    if (save < s.count) {
        if (auto* paint = s.ent[save].try_get_mut<UiPaint>()) {
            *paint = paintToken(s.look, UiToken::Accent, s.look.radiusSm);
        }
    }
}

} // namespace burnhope
