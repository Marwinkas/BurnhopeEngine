#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "host/PropGrid.hpp"
#include "asset/MaterialBake.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/Nodes.hpp"

namespace burnhope {

namespace {

void onType(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    UiState* state = matPageState(*hud);
    if (state != nullptr) {
        matApplyFlag(*hud, *state, kFlMatUnlit, index == 1 ? 1u : 0u);
    }
    saveOpenMaterial(*hud);
}

void onPack(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    UiState* state = matPageState(*hud);
    if (state != nullptr) {
        matApplyFlag(*hud, *state, kFlMatPack, index == 0 ? 0u : 1u);
        hudPaintMaterial(*hud);
    }
    saveOpenMaterial(*hud);
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

} // namespace

void hudBuildMaterialPage(Canvas& canvas, HudContext* hud, uint16_t panel) {
    if (canvas.state == nullptr || hud == nullptr || panel == kUiNone) {
        return;
    }
    UiState& s = *canvas.state;
    hud->bind.onReset = matOnReset;
    hud->bind.resetUser = hud;
    hud->previewBuf.resize(kMatSphereSide * kMatSphereSide * 4u);
    hud->atlasBuf.resize(kMatAtlasW * kMatAtlasH * 4u);
    UiFlex previewCol{};
    previewCol.direction = UiFlexDir::Column;
    previewCol.align = UiAlign::Center;
    previewCol.widthMode = UiSize::Percent;
    previewCol.width = 100.0f;
    previewCol.gap = 4.0f;
    previewCol.shrink = 0.0f;
    const uint16_t preview = uiPanel(s, panel, previewCol, paintClear());
    hud->matPreview = uiIcon(s, preview, 8, static_cast<float>(kMatSphereSide), static_cast<float>(kMatSphereSide));
    uiDrag(s, hud->matPreview, onPreviewDrag, hud);
    uiLabel(s, preview, "Material Preview");
    const char* typeName[] = {"Lit", "Unlit"};
    const char* packName[] = {"Classic", "ORMH"};
    hud->matType = hud->bind.choiceReset(s, panel, "Type", typeName, 2, onType, hud, kFlMatUnlit);
    hud->bind.checkReset(s, panel, "Double Sided", kFlMatDouble, false);
    matAddSlot(s, *hud, panel, 0);
    matAddSlot(s, *hud, panel, 1);
    matAddSlot(s, *hud, panel, 2);
    hud->bind.checkReset(s, panel, "Flip Normal Y", kFlMatFlip, true);
    hud->matPack = hud->bind.choiceReset(s, panel, "Map Type", packName, 2, onPack, hud, kFlMatPack);
    for (uint8_t slot = 3; slot < 8; ++slot) {
        matAddSlot(s, *hud, panel, slot);
    }
    matAddSlot(s, *hud, panel, 8);
    hud->bind.checkReset(s, panel, "Repeat", kFlMatRepeat, true);
    hud->bind.checkReset(s, panel, "Triplanar", kFlMatWorld, false);
    hud->bind.vec2Reset(s, panel, "Tiling", kScMatUvX, 1.0f, 1.0f);
    hud->bind.vec2Reset(s, panel, "Offset", kScMatUvOffX, 0.0f, 0.0f);
}

} // namespace burnhope
