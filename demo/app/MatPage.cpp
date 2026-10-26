#include "app/Engine.hpp"
#include <algorithm>
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"

#include "host/PropGrid.hpp"
#include "host/PropImport.hpp"
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

constexpr float kZoomMin = 0.5f;
constexpr float kZoomMax = 2.5f;
constexpr float kZoomStep = 0.1f;

void onSave(void* user, uint16_t) {
    if (auto* hud = static_cast<HudContext*>(user)) {
        saveOpenMaterial(*hud);
    }
}

void onShape(void* user, uint16_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud != nullptr) {
        hud->previewShape = static_cast<uint8_t>(index);
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

void onPreviewWheel(void* user, uint16_t, float wheel, float, float) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud != nullptr) {
        hud->previewZoom = std::clamp(hud->previewZoom + wheel * kZoomStep, kZoomMin, kZoomMax);
        hud->previewDirty = 1;
    }
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
    const PropSlot slots[] = {
        {"matPreview", &hud->matPreview},
    };
    const PropCommand commands[] = {
        {"mat.shape", onShape, hud},
        {"mat.save", onSave, hud},
    };
    const PropRole roles[] = {{"matslot", matSlotRole, hud}, {"matrow", matRowRole, hud}};
    if (!propImportAsset(s, panel, "assets/ui/material.json", "material", hud->bind, slots, 1, commands, 2, paintClear(), nullptr, hud, roles, 2)) {
        return;
    }
    uiDrag(s, hud->matPreview, onPreviewDrag, hud);
    uiDragRight(s, hud->matPreview, onPreviewOrbit, hud);
    uiWheelBind(s, hud->matPreview, onPreviewWheel, hud);
}

} // namespace burnhope
