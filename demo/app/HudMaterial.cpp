#include "app/HudBuild.hpp"

#include "Str.hpp"
#include "files/FileSession.hpp"
#include "files/Format.hpp"
#include "gfx/Material.hpp"
#include "platform/Window.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Menu.hpp"

#include <cstdio>

namespace burnhope {

constexpr uint8_t kSlotCh[7] = {0, 1, 4, 5, 6, 2, 3};
const char* kSlotName[7] = {"Albedo", "Normal", "Roughness", "Metallic", "AO", "ORM", "Emissive"};

char* hudDocPath(BhmatDoc& doc, uint8_t channel) {
    char* path[] = {doc.albedoPath, doc.normalPath, doc.ormPath, doc.emissivePath, doc.roughPath, doc.metalPath, doc.aoPath};
    return channel < 7 ? path[channel] : doc.albedoPath;
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

void hudPaintSlots(HudContext& hud) {
    UiState* state = hudUi(hud);
    if (state == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < 7; ++i) {
        uiAssetSlotText(*state, hud.liveSlot[i], leaf(hudDocPath(hud.live, kSlotCh[i])));
    }
    uiAssetSlotText(*state, hud.liveDrop, hud.livePath[0] != '\0' ? leaf(hud.livePath) : "Drop .bhmat");
}

void hudPushLive(HudContext& hud, UiState& state) {
    float* src[] = {
        &hud.live.roughness, &hud.live.metallic, &hud.live.reflectance, &hud.live.aoStrength, &hud.live.normalScale,
        &hud.live.emissiveGain, &hud.live.clearcoat, &hud.live.clearcoatRoughness, &hud.live.alphaCutoff,
        &hud.live.worldGridSize, &hud.live.triplanarSharpness, &hud.live.worldOffset[0], &hud.live.worldOffset[1],
        &hud.live.worldOffset[2], &hud.live.uvTiling[0], &hud.live.uvTiling[1], &hud.live.uvOffset[0], &hud.live.uvOffset[1],
    };
    for (float* value : src) {
        hud.bind.write(state, value, *value);
    }
    hud.uvDeg = hud.live.uvRotation * 57.2957795f;
    hud.bind.write(state, &hud.uvDeg, hud.uvDeg);
    const uint8_t* bit[] = {&hud.live.worldAligned, &hud.live.doubleSided, &hud.live.alphaCut, &hud.live.flipNormalY, &hud.live.unlit};
    for (const uint8_t* flag : bit) {
        const uint16_t id = hud.bind.idOf(flag);
        if (id < state.count) {
            uiCheckAssign(state, id, *flag);
        }
    }
    for (uint16_t i = 0; i < hud.bind.nSwatches; ++i) {
        const UiSwatchBind& sw = hud.bind.swatches[i];
        if (sw.id < state.count && sw.r != nullptr) {
            if (auto* paint = state.ent[sw.id].try_get_mut<UiPaint>()) {
                paint->r = *sw.r;
                paint->g = sw.g != nullptr ? *sw.g : 0.0f;
                paint->b = sw.b != nullptr ? *sw.b : 0.0f;
            }
        }
    }
    hudPaintSlots(hud);
    uiNotifyVisualDirty(state);
}

void hudPaintPick(HudContext& hud, UiState& state) {
    if (hud.livePick >= state.count) {
        return;
    }
    const UiChoice* choice = state.ent[hud.livePick].try_get<UiChoice>();
    if (choice == nullptr) {
        return;
    }
    uint8_t seen = 0;
    for (uint16_t child = 0; child < state.count && seen < 8; ++child) {
        if (state.parentOf[child] != choice->menu) {
            continue;
        }
        uiText(state, child, seen < hud.matListN ? hud.matName[seen] : "");
        seen = static_cast<uint8_t>(seen + 1u);
    }
    uiMenuCaption(state, hud.livePick, hud.livePath[0] != '\0' ? leaf(hud.livePath) : "None");
}

namespace {

void adopt(HudContext& hud, const BhmatDoc& doc, const char* path) {
    hud.live = doc;
    hud.liveSent = doc;
    hud.liveDirty = 0;
    if (path != nullptr) {
        std::snprintf(hud.livePath, sizeof(hud.livePath), "%s", path);
    }
    if (UiState* state = hudUi(hud)) {
        hudPushLive(hud, *state);
        hudPaintPick(hud, *state);
    }
}

void onMatPick(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || index == 0 || index >= hud->matListN || hud->scene == nullptr) {
        return;
    }
    BhmatDoc doc{};
    if (materialLoadBhmat(hud->matFile[index], doc) && sceneFrameApplyDoc(hud->scene, sceneFrameActorSelected(hud->scene), doc)) {
        adopt(*hud, doc, hud->matFile[index]);
    }
}

void onLiveBrowse(void* user, uint16_t) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->hud->host == nullptr || hit->kind < 1 || hit->kind > 7) {
        return;
    }
    hit->hud->texAsk = hit->kind;
    hit->hud->texLive = 1;
    const FileFilter filter{"Image", "png;jpg;jpeg;dds;tga;hdr"};
    (void)windowAskFile(hit->hud->host->view[hit->hud->slot].window, &filter, 1, FileAsk::Open);
}

void onLiveClear(void* user, uint16_t) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->kind < 1) {
        return;
    }
    hudDocPath(hit->hud->live, static_cast<uint8_t>(hit->kind - 1))[0] = '\0';
    hit->hud->liveDirty = 1;
    hudPaintSlots(*hit->hud);
}

void bindDoc(HudContext& hud, const char* path) {
    BhmatDoc doc{};
    if (path != nullptr && materialLoadBhmat(path, doc) && hud.scene != nullptr
        && sceneFrameApplyDoc(hud.scene, sceneFrameActorSelected(hud.scene), doc)) {
        adopt(hud, doc, path);
    }
}

void onLivePath(void* user, const char* path) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || path == nullptr || path[0] == '\0') {
        return;
    }
    if (strExtIs(path, ".bhmat")) {
        bindDoc(*hit->hud, path);
        return;
    }
    if (hit->kind < 1) {
        return;
    }
    std::snprintf(hudDocPath(hit->hud->live, static_cast<uint8_t>(hit->kind - 1)), 260, "%s", path);
    hudPaintSlots(*hit->hud);
    hit->hud->liveDirty = 1;
}

void onMatDrop(void* user, const char* path) {
    if (auto* hud = static_cast<HudContext*>(user); hud != nullptr && path != nullptr && strExtIs(path, ".bhmat")) {
        bindDoc(*hud, path);
    }
}

void blankDoc(BhmatDoc& doc) {
    doc = {};
    doc.base[0] = doc.base[1] = doc.base[2] = doc.baseA = 1.0f;
    doc.roughness = doc.reflectance = doc.alphaCutoff = 0.5f;
    doc.aoStrength = doc.normalScale = doc.worldGridSize = doc.uvTiling[0] = doc.uvTiling[1] = 1.0f;
    doc.triplanarSharpness = 4.0f;
    doc.clearcoatRoughness = 0.04f;
    doc.flipNormalY = 1;
    doc.emissiveColor[0] = doc.emissiveColor[1] = doc.emissiveColor[2] = 1.0f;
}

void storeLive(HudContext& hud, const char* stem) {
    UiState* state = hudUi(hud);
    const char* dir = state != nullptr && fileBook(*state).cwd[0] != '\0' ? fileBook(*state).cwd : hud.projectRoot;
    char dst[640]{};
    if (dir == nullptr || dir[0] == '\0' || hud.scene == nullptr) {
        return;
    }
    uniquePath(dst, static_cast<int>(sizeof(dst)), dir, stem, ".bhmat");
    if (!materialSaveBhmat(dst, hud.live)) {
        return;
    }
    if (sceneFrameApplyDoc(hud.scene, sceneFrameActorSelected(hud.scene), hud.live)) {
        adopt(hud, hud.live, dst);
    }
    if (state != nullptr) {
        fileBook(*state).reload();
    }
}

void onNewMat(void* user, uint16_t) {
    if (auto* hud = static_cast<HudContext*>(user)) {
        blankDoc(hud->live);
        storeLive(*hud, "Material");
    }
}

void onDupMat(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud != nullptr && hud->scene != nullptr && sceneFrameCloneMaterial(hud->scene, sceneFrameActorSelected(hud->scene))) {
        storeLive(*hud, "Material");
    }
}

void onSaveMat(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud != nullptr && hud->livePath[0] != '\0' && materialSaveBhmat(hud->livePath, hud->live)) {
        hud->liveDirty = 0;
    }
}

void onResetUv(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->live.uvTiling[0] = hud->live.uvTiling[1] = 1.0f;
    hud->live.uvOffset[0] = hud->live.uvOffset[1] = hud->live.uvRotation = hud->uvDeg = 0.0f;
    if (UiState* state = hudUi(*hud)) {
        hudPushLive(*hud, *state);
    }
}

} // namespace

void hudBuildActorMaterial(Canvas& canvas, HudContext* hud, uint16_t panel) {
    if (canvas.state == nullptr || hud == nullptr) {
        return;
    }
    UiState& state = *canvas.state;
    static const char* labels[8] = {"None", "None", "None", "None", "None", "None", "None", "None"};
    hud->livePick = uiChoice(state, panel, {.labels = labels, .count = 8, .onPick = onMatPick, .user = hud});
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.heightMode = UiSize::Px;
    row.height = 28.0f;
    row.gap = 4.0f;
    const uint16_t tools = uiPanel(state, panel, row, paintToken(state.look, UiToken::Content, 0.0f));
    uiButton(state, tools, {.label = "+ New", .width = 72.0f, .height = 24.0f, .onClick = onNewMat, .user = hud});
    uiButton(state, tools, {.label = "Duplicate", .width = 88.0f, .height = 24.0f, .onClick = onDupMat, .user = hud});
    hud->liveSave = uiButton(state, tools, {.label = "Save", .width = 64.0f, .height = 24.0f, .onClick = onSaveMat, .user = hud});
    hud->liveDrop = uiAssetSlot(state, panel, {.label = "Drop .bhmat", .onPath = onMatDrop, .user = hud});
    hud->bind.swatch(state, panel, "Base Color", &hud->live.base[0], &hud->live.base[1], &hud->live.base[2], 1, 1, 1);
    hud->bind.swatch(state, panel, "Emissive", &hud->live.emissiveColor[0], &hud->live.emissiveColor[1], &hud->live.emissiveColor[2], 1, 1, 1);
    const UiSlideSpec spec[] = {
        {"Roughness", &hud->live.roughness, 0.5f, 0.04f, 1}, {"Metallic", &hud->live.metallic, 0, 0, 1},
        {"Reflectance", &hud->live.reflectance, 0.5f, 0, 1}, {"AO", &hud->live.aoStrength, 1, 0, 1},
        {"Normal Strength", &hud->live.normalScale, 1, 0, 3}, {"Emissive Gain", &hud->live.emissiveGain, 0, 0, 50},
        {"Clearcoat", &hud->live.clearcoat, 0, 0, 1}, {"Clearcoat Roughness", &hud->live.clearcoatRoughness, 0.04f, 0.04f, 1},
        {"Alpha Cutoff", &hud->live.alphaCutoff, 0.5f, 0, 1}, {"World Grid Size", &hud->live.worldGridSize, 1, 0.05f, 25},
        {"Triplanar Sharpness", &hud->live.triplanarSharpness, 4, 1, 16},
        {"World Offset X", &hud->live.worldOffset[0], 0, -50, 50}, {"World Offset Y", &hud->live.worldOffset[1], 0, -50, 50},
        {"World Offset Z", &hud->live.worldOffset[2], 0, -50, 50}, {"UV Tiling X", &hud->live.uvTiling[0], 1, 0.05f, 32},
        {"UV Tiling Y", &hud->live.uvTiling[1], 1, 0.05f, 32}, {"UV Offset X", &hud->live.uvOffset[0], 0, -10, 10},
        {"UV Offset Y", &hud->live.uvOffset[1], 0, -10, 10}, {"UV Rotation", &hud->uvDeg, 0, -180, 180},
    };
    hud->bind.slides(state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    hud->bind.check(state, panel, "World-Aligned", &hud->live.worldAligned, false);
    hud->bind.check(state, panel, "Double Sided", &hud->live.doubleSided, false);
    hud->bind.check(state, panel, "Alpha Cutout", &hud->live.alphaCut, false);
    hud->bind.check(state, panel, "Flip Normal Y", &hud->live.flipNormalY, true);
    hud->bind.check(state, panel, "Unlit", &hud->live.unlit, false);
    uiButton(state, panel, {.label = "Reset UV", .fillWidth = true, .onClick = onResetUv, .user = hud});
    for (uint8_t i = 0; i < 7; ++i) {
        hud->liveHit[i].hud = hud;
        hud->liveHit[i].kind = static_cast<uint8_t>(kSlotCh[i] + 1u);
        hud->liveSlot[i] = uiAssetSlot(state, panel, {
            .label = kSlotName[i], .onBrowse = onLiveBrowse, .onClear = onLiveClear, .onPath = onLivePath, .user = &hud->liveHit[i]});
    }
}

} // namespace burnhope
