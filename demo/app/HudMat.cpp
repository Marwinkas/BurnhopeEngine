#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "asset/ProjectDoc.hpp"
#include "files/Browser.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Window.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Scroll.hpp"
#include "Str.hpp"
#include "ui/State.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {

void openMaterialFile(HudContext& hud, const char* path) {
    if (path == nullptr || path[0] == '\0' || !materialDocRead(path, hud.matDoc)) {
        return;
    }
    std::snprintf(hud.matPath, sizeof(hud.matPath), "%s", path);
    std::snprintf(hud.matDoc.file, sizeof(hud.matDoc.file), "%s", path);
    hud.matSent = hud.matDoc;
    hud.atlasDirty = 1;
    hud.previewDirty = 1;
    hud.matOpen = 1;
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud.tool.canvas->state;
    auto put = [&](uint16_t index, float value) { hud.bind.write(s, index, value); };
    put(kScMatBase0, hud.matDoc.base[0]);
    put(kScMatBase1, hud.matDoc.base[1]);
    put(kScMatBase2, hud.matDoc.base[2]);
    put(kScMatRough, hud.matDoc.roughness);
    put(kScMatMetal, hud.matDoc.metallic);
    put(kScMatEmissive, hud.matDoc.emissive);
    put(kScMatAlbedoGain, hud.matDoc.albedoGain);
    put(kScMatNormalGain, hud.matDoc.normalGain);
    put(kScMatOrmGain, hud.matDoc.ormGain);
    put(kScMatEmissiveGain, hud.matDoc.emissiveGain > 0.0f ? hud.matDoc.emissiveGain : hud.matDoc.emissive);
    put(kScMatReflect, hud.matDoc.reflectance);
    put(kScMatAo, hud.matDoc.aoStrength);
    put(kScMatNormal, hud.matDoc.normalScale);
    put(kScMatCoat, hud.matDoc.clearcoat);
    put(kScMatCoatRough, hud.matDoc.clearcoatRoughness);
    put(kScMatGrid, hud.matDoc.worldGridSize);
    put(kScMatSharp, hud.matDoc.triplanarSharpness);
    put(kScMatUvX, hud.matDoc.uvTiling[0]);
    put(kScMatUvY, hud.matDoc.uvTiling[1]);
    put(kScMatUvRot, hud.matDoc.uvRotation * 57.2957795f);
    put(kScMatEm0, hud.matDoc.emissiveColor[0]);
    put(kScMatEm1, hud.matDoc.emissiveColor[1]);
    put(kScMatEm2, hud.matDoc.emissiveColor[2]);
    put(kScMatHeight, hud.matDoc.heightScale);
    put(kScMatOpacity, hud.matDoc.baseA);
    put(kScMatUvOffX, hud.matDoc.uvOffset[0]);
    put(kScMatUvOffY, hud.matDoc.uvOffset[1]);
    put(kScMatCutoff, hud.matDoc.alphaCutoff);
    const uint16_t bit[] = {kFlMatWorld, kFlMatDouble, kFlMatAlpha, kFlMatFlip, kFlMatUnlit, kFlMatRepeat, kFlMatPack};
    const uint8_t on[] = {
        hud.matDoc.worldAligned, hud.matDoc.doubleSided, hud.matDoc.alphaCut, hud.matDoc.flipNormalY, hud.matDoc.unlit,
        hud.matDoc.repeat, hud.matDoc.mapPack};
    for (uint16_t i = 0; i < 7u; ++i) {
        if (bit[i] < 64u) {
            hud.block.flags[bit[i]] = on[i];
        }
        const uint16_t id = hud.bind.idOf(bit[i], kBindFlag);
        if (id < s.count) {
            uiCheckAssign(s, id, on[i]);
        }
    }
    hudPaintMaterial(hud);
    uiPropShowMask(s, kSecMatFile);
    scrollReset(s, hud.tool.propPage);
}

bool matSame(const MaterialInfo& a, const MaterialInfo& b) {
    return std::memcmp(&a, &b, sizeof(MaterialInfo)) == 0;
}

void saveOpenMaterial(HudContext& hud) {
    if (hud.matOpen == 0 || hud.matPath[0] == '\0' || matSame(hud.matDoc, hud.matSent)) {
        return;
    }
    if (!materialDocWrite(hud.matPath, hud.matDoc)) {
        return;
    }
    hud.matSent = hud.matDoc;
    hud.matListed = 0;
    if (hud.scene == nullptr) {
        return;
    }
    const uint8_t sel = sceneFrameActorSelected(hud.scene);
    if (sceneFrameActorKind(hud.scene, sel) == static_cast<uint8_t>(SceneActorKind::Prop)) {
        if (!sceneFrameApplyDoc(hud.scene, sel, hud.matDoc)) {
            return;
        }
    }
}

void onMaterialAct(void* user, uint16_t) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->hud->matOpen == 0) {
        return;
    }
    if (hit->hud->host == nullptr || hit->hud->slot < 0 || hit->kind < 1 || hit->kind > 9) {
        return;
    }
    hit->hud->texAsk = hit->kind;
    const FileFilter filter{"Image", "png;jpg;jpeg;dds;tga;hdr"};
    (void)windowAskFile(hit->hud->host->view[hit->hud->slot].window, &filter, 1, FileAsk::Open);
}

void onFileOpen(void* user, const char* path, int isDir) {
    if (isDir != 0 || user == nullptr || path == nullptr || !strExtIs(path, ".bhmat")) {
        return;
    }
    openMaterialFile(*static_cast<HudContext*>(user), path);
}

namespace {

constexpr size_t kMatListCap = 128;

bool takeMat(void* user, const char* name, const char* path) {
    auto* hud = static_cast<HudContext*>(user);
    MatLeaf leaf{};
    std::snprintf(leaf.name, sizeof(leaf.name), "%s", name);
    std::snprintf(leaf.path, sizeof(leaf.path), "%s", path);
    hud->mats.push_back(leaf);
    return hud->mats.size() < kMatListCap;
}

void fillMatList(HudContext& hud) {
    hud.mats.clear();
    hud.mats.push_back({});
    std::snprintf(hud.mats.front().name, sizeof(hud.mats.front().name), "None");
    if (hud.projectRoot[0] != '\0') {
        projectListMaterials(hud.projectRoot, takeMat, &hud);
    }
}

} // namespace

void hudTakeTexture(HudContext& hud, uint8_t channel, const char* path) {
    if (path == nullptr || path[0] == '\0' || channel > 8u) {
        return;
    }
    std::snprintf(hudDocPath(hud.matDoc, channel), 260, "%s", path);
    hud.texLive = 0;
    hudCommitMaps(hud, channel);
    hud.atlasDirty = 1;
    hud.previewDirty = 1;
    hudPaintMaterial(hud);
    saveOpenMaterial(hud);
}

void hudMaterialSync(Engine& e, Canvas& canvas) {
    if (canvas.state == nullptr || e.scene == nullptr) {
        return;
    }
    HudContext& hud = e.hud;
    UiState& state = *canvas.state;
    if (hud.matListed == 0 && hud.projectRoot[0] != '\0') {
        fillMatList(hud);
        hud.matListed = 1;
        hudPaintPick(hud, state);
    }
    const uint8_t sel = sceneFrameActorSelected(e.scene);
    if (sel == hud.liveActor) {
        return;
    }
    hud.liveActor = sel;
    hud.livePath[0] = '\0';
    if (sel != kSceneActorNone && sceneFrameReadDoc(e.scene, sel, hud.live)) {
        hud.liveSent = hud.live;
        if (hud.live.file[0] != '\0') {
            std::snprintf(hud.livePath, sizeof(hud.livePath), "%s", hud.live.file);
        }
        hudPaintPick(hud, state);
    }
}

void hudMaterialDrag(HudContext& hud, UiState& state) {
    if (hud.scene == nullptr) {
        return;
    }
    uint8_t mark = kSceneActorNone;
    float x = 0.0f;
    float y = 0.0f;
    const char* name = filesDraggedOver(state, hud.viewPane, x, y);
    if (name != nullptr && strExtIs(name, ".bhmat")) {
        mark = sceneFramePick(hud.scene, x, y);
    }
    sceneFrameMark(hud.scene, mark);
}

void hudDropImport(void* user, const char* path, float x, float y) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || path == nullptr) {
        return;
    }
    if (strExtIs(path, ".bhmat")) {
        BhmatDoc doc{};
        if (hud->scene != nullptr && materialLoadBhmat(path, doc)) {
            std::snprintf(doc.file, sizeof(doc.file), "%s", path);
            const uint8_t hit = sceneFramePick(hud->scene, x, y);
            if (hit != kSceneActorNone && sceneFrameApplyDoc(hud->scene, hit, doc)) {
                sceneFrameActorSelect(hud->scene, hit);
                hud->liveActor = 0xff;
            }
        }
        return;
    }
    projectImportModel(*hud, path);
}

void hudDragBadge(void* user, char* dst, uint8_t cap) {
    auto* hud = static_cast<HudContext*>(user);
    if (dst == nullptr || cap == 0) {
        return;
    }
    dst[0] = '\0';
    UiState* state = hud != nullptr ? hudUi(*hud) : nullptr;
    float x = 0.0f;
    float y = 0.0f;
    const char* name = state != nullptr ? filesDraggedOver(*state, hud->viewPane, x, y) : nullptr;
    if (name == nullptr || !strExtIs(name, ".bhmat")) {
        return;
    }
    char actor[24] = "Scene";
    const uint8_t mark = sceneFramePick(hud->scene, x, y);
    uint8_t kind = 0;
    if (mark != kSceneActorNone) {
        sceneFrameActorName(hud->scene, mark, actor, sizeof(actor), &kind);
    }
    std::snprintf(dst, cap, "%s -> %s", name, actor);
}

} // namespace burnhope
