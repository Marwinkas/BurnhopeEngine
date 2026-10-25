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

const char* leaf(const char* path) {
    const char* name = path != nullptr ? path : "";
    for (const char* p = name; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

UiState* hudUi(HudContext& hud) {
    if (hud.host == nullptr || hud.slot < 0) {
        return nullptr;
    }
    return hud.host->view[hud.slot].canvas.state.get();
}

void hudPaintPick(HudContext& hud, UiState& state) {
    if (hud.livePick >= state.count) {
        return;
    }
    const UiChoice* choice = state.ent[hud.livePick].try_get<UiChoice>();
    if (choice == nullptr) {
        return;
    }
    uint16_t rows = 0;
    for (uint16_t child = 0; child < state.count; ++child) {
        if (state.parentOf[child] == choice->menu) {
            rows = static_cast<uint16_t>(rows + 1u);
        }
    }
    while (rows < hud.mats.size() && rows < 128 && state.count + 4u < kUiCap) {
        uiMenuRow(state, choice->menu, hud.mats[rows].name, nullptr, nullptr, rows, hud.livePick, kUiNone);
        rows = static_cast<uint16_t>(rows + 1u);
    }
    uint16_t seen = 0;
    for (uint16_t child = 0; child < state.count; ++child) {
        if (state.parentOf[child] != choice->menu) {
            continue;
        }
        const bool live = seen < hud.mats.size();
        uiText(state, child, live ? hud.mats[seen].name : "");
        uiShow(state, child, live);
        seen = static_cast<uint16_t>(seen + 1u);
    }
    uiMenuCaption(state, hud.livePick, hud.livePath[0] != '\0' ? leaf(hud.livePath) : "None");
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
    std::snprintf(doc.file, sizeof(doc.file), "%s", path);
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
    std::snprintf(doc.file, sizeof(doc.file), "%s", path);
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
    if (hud->livePick < state.count) {
        if (auto* flex = state.ent[hud->livePick].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Percent;
            flex->width = 100.0f;
            flex->grow = 1.0f;
        }
    }
    hud->liveDrop = uiAssetSlot(state, panel, {.label = "Drop .bhmat", .onPath = onMatDrop, .user = hud});
}

} // namespace burnhope
