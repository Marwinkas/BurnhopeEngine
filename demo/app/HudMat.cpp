#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/SceneBlockMap.hpp"

#include "asset/ProjectDoc.hpp"
#include "files/Browser.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Window.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Menu.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Scroll.hpp"
#include "Str.hpp"
#include "ui/Nodes.hpp"
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
    if (hud.matTitle < s.count) {
        const char* leaf = std::strrchr(path, '/');
        leaf = leaf != nullptr ? leaf + 1 : path;
        const char* dot = std::strrchr(leaf, '.');
        const int stem = dot != nullptr && dot > leaf ? static_cast<int>(dot - leaf) : static_cast<int>(std::strlen(leaf));
        char title[160]{};
        std::snprintf(title, sizeof(title), "Editing: %.*s", stem, leaf);
        uiText(s, hud.matTitle, title);
    }
    auto put = [&](uint16_t index, float value) { hud.bind.write(s, index, value); };
    for (const ScalarField& field : kDocFields) {
        put(field.scalar, scalarLoad(hud.matDoc, field.offset));
    }
    put(kScMatEmissiveGain, hud.matDoc.emissiveGain > 0.0f ? hud.matDoc.emissiveGain : hud.matDoc.emissive);
    put(kScMatUvRot, hud.matDoc.uvRotation * 57.2957795f);
    const uint16_t bit[] = {kFlMatWorld, kFlMatDouble, kFlMatAlpha, kFlMatFlip, kFlMatUnlit, kFlMatRepeat, kFlMatPack};
    const uint8_t on[] = {
        hud.matDoc.worldAligned, hud.matDoc.doubleSided, hud.matDoc.alphaCut, hud.matDoc.flipNormalY, hud.matDoc.unlit,
        hud.matDoc.repeat, hud.matDoc.mapPack};
    for (uint16_t i = 0; i < 7u; ++i) {
        if (bit[i] < 64u) {
            hud.block.flags[bit[i]] = on[i];
        }
        uiCheckAssign(s, hud.bind.idOf(bit[i], kBindFlag), on[i]);
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

namespace {

constexpr size_t kTexListCap = 512;

bool takeTexture(void* user, const char* name, const char* path) {
    auto* hud = static_cast<HudContext*>(user);
    MatLeaf leaf{};
    std::snprintf(leaf.name, sizeof(leaf.name), "%s", name);
    std::snprintf(leaf.path, sizeof(leaf.path), "%s", path);
    hud->texList.push_back(leaf);
    return hud->texList.size() < kTexListCap;
}

const char* texLabel(void* user, uint16_t index) {
    auto* hud = static_cast<HudContext*>(user);
    return index < hud->texList.size() ? hud->texList[index].name : "";
}

void texPicked(void* user, uint16_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (index < hud->texList.size()) {
        hudTakeTexture(*hud, hud->texChannel, hud->texList[index].path);
    }
}

} // namespace

void onMaterialAct(void* user, uint16_t id) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->hud->matOpen == 0 || hit->kind < 1 || hit->kind > 9) {
        return;
    }
    HudContext& hud = *hit->hud;
    UiState* state = hudUi(hud);
    if (state == nullptr || hud.projectRoot[0] == '\0') {
        return;
    }
    hud.texList.clear();
    projectListTextures(hud.projectRoot, takeTexture, &hud);
    if (hud.texList.empty()) {
        return;
    }
    hud.texChannel = static_cast<uint8_t>(hit->kind - 1u);
    uiMenuList(*state, id, static_cast<uint16_t>(hud.texList.size()), texLabel, texPicked, &hud);
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
