#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "asset/MaterialBake.hpp"
#include "asset/ProjectDoc.hpp"
#include "files/Browser.hpp"
#include "ui/rhi/Canvas.hpp"
#include "ui/Panel.hpp"

#include <algorithm>

namespace burnhope {

namespace {

Canvas* previewCanvas(HudContext& hud) {
    if (hud.tool.canvas != nullptr && hud.tool.canvas->device != nullptr) {
        return hud.tool.canvas;
    }
    if (hud.host != nullptr && hud.slot >= 0) {
        return &hud.host->view[hud.slot].canvas;
    }
    return nullptr;
}

} // namespace

void hudCommitMaps(HudContext& hud, uint8_t channel) {
    if (!materialCommitMaps(hud.matDoc, hud.matPath, channel)) {
        return;
    }
    if (UiState* state = hudUi(hud)) {
        filesThumbsInvalidate(*state);
        hud.bind.write(*state, kScMatHeight, hud.matDoc.heightScale);
    }
}

void hudBakeMaterialView(HudContext& hud) {
    UiState* state = hud.tool.canvas != nullptr ? hud.tool.canvas->state : nullptr;
    if (state != nullptr) {
        filesThumbsFrame(*state);
    }
    if (state != nullptr && uiCaptured(*state) != hud.matPreview) {
        hud.previewDown = 0;
    }
    if (hud.matOpen != 0) {
        // Transparency follows the opacity: a material is cut out as soon as it has an alpha map or an opacity below one.
        const uint8_t cut = hud.matDoc.baseA < 0.999f || hud.matDoc.alphaPath[0] != '\0' ? 1u : 0u;
        if (hud.matDoc.alphaCut != cut) {
            hud.matDoc.alphaCut = cut;
            hud.block.flags[kFlMatAlpha] = cut;
            saveOpenMaterial(hud);
        }
    }
    bool changed = hud.atlasDirty != 0;
    if (hud.matEdited) {
        hud.matEdited = false;
        hud.atlasDirty = 1;
        hud.previewDirty = 1;
        changed = true;
    }
    if (!matSame(hud.matDoc, hud.previewSeen)) {
        hud.previewSeen = hud.matDoc;
        hud.previewDirty = 1;
        changed = true;
    }
    if (hud.previewTint[0] != hud.matDoc.base[0] || hud.previewTint[1] != hud.matDoc.base[1] || hud.previewTint[2] != hud.matDoc.base[2]) {
        std::copy(hud.matDoc.base, hud.matDoc.base + 3, hud.previewTint);
        hud.previewDirty = 1;
        changed = true;
    }
    if (changed && state != nullptr && hud.matOpen != 0 && hud.matPath[0] != '\0') {
        // saveOpenMaterial already wrote the document this frame, so the browser's baker reads the edited file.
        filesThumbInvalidate(*state, hud.matPath);
    }
    matThumbsFlush(hud.thumbs);
    if (hud.atlasDirty == 0 && hud.previewDirty == 0) {
        return;
    }
    hud.atlasBuf.resize(kMatAtlasW * kMatAtlasH * 4u);
    hud.previewBuf.resize(kMatSphereSide * kMatSphereSide * 4u);
    Canvas* canvas = previewCanvas(hud);
    if (canvas == nullptr || canvas->device == nullptr) {
        return;
    }
    if (hud.atlasDirty != 0) {
        materialBakeAtlas(hud.matDoc, hud.matPath, hud.atlasBuf.data());
        if (canvasUploadPhoto(*canvas, 9, hud.atlasBuf.data(), kMatAtlasW, kMatAtlasH)) {
            hud.atlasDirty = 0;
            hud.previewDirty = 1;
        }
    }
    if (hud.previewDirty != 0) {
        materialBakePreview(hud.matDoc, hud.atlasBuf.data(), static_cast<MatShape>(hud.previewShape), hud.previewYaw, hud.previewPitch, hud.previewZoom, hud.previewBuf.data());
        if (canvasUploadPhoto(*canvas, 8, hud.previewBuf.data(), kMatSphereSide, kMatSphereSide)) {
            hud.previewDirty = 0;
        }
    }
}

} // namespace burnhope
