#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "Str.hpp"
#include "gfx/Material.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Menu.hpp"

#include <cstdio>

namespace burnhope {

char* hudDocPath(BhmatDoc& doc, uint8_t channel) {
    char* path[] = {
        doc.albedoPath, doc.normalPath, doc.ormPath, doc.emissivePath, doc.roughPath, doc.metalPath, doc.aoPath,
        doc.heightPath, doc.alphaPath};
    return channel < 9 ? path[channel] : doc.alphaPath;
}

UiState* hudUi(HudContext& hud) {
    if (hud.host == nullptr || hud.slot < 0) {
        return nullptr;
    }
    return hud.host->view[hud.slot].canvas.state;
}

const char* matNameAt(void* user, uint16_t index) {
    return static_cast<HudContext*>(user)->mats[index].name;
}

void hudPaintPick(HudContext& hud, UiState& state) {
    uiChoiceSync(state, hud.livePick, static_cast<uint16_t>(hud.mats.size()), matNameAt, &hud, hud.livePath[0] != '\0' ? strPathLeaf(hud.livePath) : "None");
}

namespace {

void onMatPick(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || index == 0 || index >= hud->mats.size() || hud->scene == nullptr) {
        return;
    }
    BhmatDoc doc{};
    const char* path = hud->mats[index].path;
    if (!materialLoadBhmat(path, doc)) {
        return;
    }
    strCopyCap(doc.file, sizeof(doc.file), path);
    if (sceneFrameApplyDoc(hud->scene, sceneFrameActorSelected(hud->scene), doc)) {
        hud->live = doc;
        hud->liveSent = doc;
        std::snprintf(hud->livePath, sizeof(hud->livePath), "%s", path);
        if (UiState* state = hudUi(*hud)) {
            hudPaintPick(*hud, *state);
        }
        openMaterialFile(*hud, path);
    }
}

void bindDoc(HudContext& hud, const char* path) {
    BhmatDoc doc{};
    if (path == nullptr || !materialLoadBhmat(path, doc) || hud.scene == nullptr) {
        return;
    }
    strCopyCap(doc.file, sizeof(doc.file), path);
    if (!sceneFrameApplyDoc(hud.scene, sceneFrameActorSelected(hud.scene), doc)) {
        return;
    }
    hud.live = doc;
    hud.liveSent = doc;
    std::snprintf(hud.livePath, sizeof(hud.livePath), "%s", path);
    if (UiState* state = hudUi(hud)) {
        hudPaintPick(hud, *state);
    }
    openMaterialFile(hud, path);
}

void onMatDrop(void* user, const char* path) {
    if (auto* hud = static_cast<HudContext*>(user); hud != nullptr && path != nullptr && strExtIs(path, ".bhmat")) {
        bindDoc(*hud, path);
    }
}

} // namespace

void hudBuildActorMaterial(Canvas& canvas, HudContext* hud, uint16_t panel) {
    if (canvas.state == nullptr || hud == nullptr) {
        return;
    }
    UiState& state = *canvas.state;
    static const char* labels[] = {"None"};
    hud->livePick = uiChoice(state, panel, {.labels = labels, .count = 1, .width = 220.0f, .onPick = onMatPick, .user = hud});
    uiSetWidthPercent(state, hud->livePick, 100.0f);
    uiSetGrow(state, hud->livePick, 1.0f, -1.0f);
    hud->liveDrop = uiAssetSlot(state, panel, {.label = "Drop .bhmat", .onPath = onMatDrop, .user = hud});
}

} // namespace burnhope
