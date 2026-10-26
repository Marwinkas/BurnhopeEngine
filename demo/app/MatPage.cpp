#include "app/Engine.hpp"
#include <algorithm>
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "host/PropGrid.hpp"
#include "asset/MaterialBake.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Segment.hpp"
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

void onShape(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud != nullptr) {
        hud->previewShape = index;
        hud->previewDirty = 1;
    }
}

// Both buttons turn the shape by the pointer travel since the last step.
void orbitPreview(HudContext& hud, float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }
    hud.previewYaw += dx * 0.015f;
    hud.previewPitch = std::clamp(hud.previewPitch + dy * 0.015f, -1.4f, 1.4f);
    hud.previewDirty = 1;
}

void onPreviewOrbit(void* user, uint16_t, float dx, float dy) {
    if (auto* hud = static_cast<HudContext*>(user)) {
        orbitPreview(*hud, dx, dy);
    }
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
    orbitPreview(*hud, dx, dy);
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
    const char* shapeName[] = {"Sphere", "Cube", "Plane"};
    (void)uiSegment(s, preview, {.labels = shapeName, .count = 3, .selected = hud->previewShape, .onPick = onShape, .user = hud});
    hud->matPreview = uiIcon(s, preview, 8, static_cast<float>(kMatSphereSide), static_cast<float>(kMatSphereSide));
    uiDrag(s, hud->matPreview, onPreviewDrag, hud);
    uiDragRight(s, hud->matPreview, onPreviewOrbit, hud);
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
