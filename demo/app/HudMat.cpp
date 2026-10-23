#include "app/HudBuild.hpp"

#include "asset/ProjectDoc.hpp"
#include "files/FileSession.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Window.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/PropGrid.hpp"
#include "ui/widget/Scroll.hpp"
#include "Str.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {

const char* texLeaf(const char* path, const char* fallback) {
    if (path == nullptr || path[0] == '\0') {
        return fallback;
    }
    const char* leaf = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            leaf = p + 1;
        }
    }
    return leaf[0] != '\0' ? leaf : fallback;
}

void paintMatTex(HudContext& hud) {
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr) {
        return;
    }
    const char* fallback[] = {"Albedo", "Normal", "ORM", "Emissive"};
    const char* path[] = {
        hud.matDoc.albedoPath, hud.matDoc.normalPath, hud.matDoc.ormPath, hud.matDoc.emissivePath};
    UiState& s = *hud.tool.canvas->state;
    for (uint8_t i = 0; i < 4; ++i) {
        uiText(s, hud.matTex[i], texLeaf(path[i], fallback[i]));
    }
}

void openMaterialFile(HudContext& hud, const char* path) {
    if (path == nullptr || path[0] == '\0' || !materialDocRead(path, hud.matDoc)) {
        return;
    }
    std::snprintf(hud.matPath, sizeof(hud.matPath), "%s", path);
    hud.matSent = hud.matDoc;
    hud.matOpen = 1;
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud.tool.canvas->state;
    auto put = [&](float* dst, float value) { hud.bind.write(s, dst, value); };
    put(&hud.matDoc.base[0], hud.matDoc.base[0]);
    put(&hud.matDoc.base[1], hud.matDoc.base[1]);
    put(&hud.matDoc.base[2], hud.matDoc.base[2]);
    put(&hud.matDoc.roughness, hud.matDoc.roughness);
    put(&hud.matDoc.metallic, hud.matDoc.metallic);
    put(&hud.matDoc.emissive, hud.matDoc.emissive);
    put(&hud.matDoc.albedoGain, hud.matDoc.albedoGain);
    put(&hud.matDoc.normalGain, hud.matDoc.normalGain);
    put(&hud.matDoc.ormGain, hud.matDoc.ormGain);
    put(&hud.matDoc.emissiveGain, hud.matDoc.emissiveGain);
    put(&hud.matDoc.reflectance, hud.matDoc.reflectance);
    put(&hud.matDoc.aoStrength, hud.matDoc.aoStrength);
    put(&hud.matDoc.normalScale, hud.matDoc.normalScale);
    put(&hud.matDoc.clearcoat, hud.matDoc.clearcoat);
    put(&hud.matDoc.clearcoatRoughness, hud.matDoc.clearcoatRoughness);
    put(&hud.matDoc.worldGridSize, hud.matDoc.worldGridSize);
    put(&hud.matDoc.triplanarSharpness, hud.matDoc.triplanarSharpness);
    put(&hud.matDoc.uvTiling[0], hud.matDoc.uvTiling[0]);
    put(&hud.matDoc.uvTiling[1], hud.matDoc.uvTiling[1]);
    put(&hud.matDoc.uvRotation, hud.matDoc.uvRotation);
    const uint8_t* bit[] = {&hud.matDoc.worldAligned, &hud.matDoc.doubleSided, &hud.matDoc.alphaCut};
    for (const uint8_t* flag : bit) {
        const uint16_t id = hud.bind.idOf(flag);
        if (id < s.count) {
            if (auto* check = s.ent[id].try_get_mut<UiCheck>()) {
                check->on = *flag;
            }
        }
    }
    paintMatTex(hud);
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
    if (hit->hud->host == nullptr || hit->hud->slot < 0 || hit->kind < 1 || hit->kind > 4) {
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

namespace fs = std::filesystem;

void fillMatList(HudContext& hud) {
    hud.matListN = 1;
    std::snprintf(hud.matName[0], sizeof(hud.matName[0]), "None");
    hud.matFile[0][0] = '\0';
    if (hud.projectRoot[0] == '\0') {
        return;
    }
    const fs::path dirs[2] = {fs::path(hud.projectRoot), fs::path(hud.projectRoot) / "materials"};
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            continue;
        }
        for (const fs::directory_entry& ent : fs::directory_iterator(dir, ec)) {
            if (hud.matListN >= 8 || ec || !ent.is_regular_file(ec)) {
                continue;
            }
            const std::string name = ent.path().filename().string();
            if (!strExtIs(name.c_str(), ".bhmat")) {
                continue;
            }
            std::snprintf(hud.matName[hud.matListN], 48, "%s", name.c_str());
            std::snprintf(hud.matFile[hud.matListN], 260, "%s", ent.path().string().c_str());
            hud.matListN = static_cast<uint8_t>(hud.matListN + 1u);
        }
    }
}

bool saveLive(HudContext& hud) {
    if (hud.livePath[0] == '\0' || !materialSaveBhmat(hud.livePath, hud.live)) {
        return false;
    }
    hud.liveDirty = 0;
    return true;
}

} // namespace

void hudTakeTexture(HudContext& hud, uint8_t channel, const char* path) {
    if (path == nullptr || path[0] == '\0' || channel > 6u) {
        return;
    }
    std::snprintf(hudDocPath(hud.texLive != 0 ? hud.live : hud.matDoc, channel), 260, "%s", path);
    if (hud.texLive == 0) {
        paintMatTex(hud);
        saveOpenMaterial(hud);
        if (hud.scene != nullptr) {
            sceneFrameQueueTexture(hud.scene, sceneFrameActorSelected(hud.scene), channel, path);
        }
        return;
    }
    hudPaintSlots(hud);
    hud.liveDirty = 1;
    if (hud.scene != nullptr && sceneFrameApplyDoc(hud.scene, sceneFrameActorSelected(hud.scene), hud.live)) {
        hud.liveSent = hud.live;
    }
}

void hudMaterialSync(Engine& e, Canvas& canvas) {
    if (canvas.state == nullptr || e.scene == nullptr) {
        return;
    }
    HudContext& hud = e.hud;
    UiState& state = *canvas.state;
    const uint8_t sel = sceneFrameActorSelected(e.scene);
    if (sel != hud.liveActor) {
        hud.liveActor = sel;
        hud.livePath[0] = '\0';
        if (sel != kSceneActorNone && sceneFrameReadDoc(e.scene, sel, hud.live)) {
            hud.liveSent = hud.live;
            fillMatList(hud);
            hudPushLive(hud, state);
            hudPaintPick(hud, state);
        }
        return;
    }
    if (sel == kSceneActorNone) {
        return;
    }
    if (state.capture == hud.bind.idOf(&hud.uvDeg)) {
        hud.live.uvRotation = hud.uvDeg * 0.017453292f;
    }
    if (state.capture == hud.bind.idOf(&hud.live.emissiveGain)) {
        hud.live.emissive = hud.live.emissiveGain;
    }
    if (std::memcmp(&hud.live, &hud.liveSent, sizeof(BhmatDoc)) != 0 && sceneFrameApplyDoc(e.scene, sel, hud.live)) {
        hud.liveSent = hud.live;
        hud.liveDirty = 1;
    }
    if (hud.liveDirty != 0 && state.capture == kUiNone) {
        saveLive(hud);
    }
}

void hudMaterialDrag(HudContext& hud, UiState& state) {
    if (hud.scene == nullptr) {
        return;
    }
    uint8_t mark = kSceneActorNone;
    const UiDragDrop* drag = state.world.try_get<UiDragDrop>();
    if (drag != nullptr && drag->live != 0 && hud.viewPane < state.count && hitBox(uiNodeBox(state, hud.viewPane), drag->dropX, drag->dropY)) {
        Browser& book = fileBook(state);
        if (drag->srcIndex < book.items.size() && strExtIs(book.items[drag->srcIndex].name.c_str(), ".bhmat")) {
            mark = sceneFramePick(hud.scene, drag->dropX, drag->dropY);
        }
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
    const UiDragDrop* drag = state != nullptr ? state->world.try_get<UiDragDrop>() : nullptr;
    if (hud == nullptr || drag == nullptr || drag->live == 0 || hud->viewPane >= state->count) {
        return;
    }
    if (!hitBox(uiNodeBox(*state, hud->viewPane), drag->dropX, drag->dropY)) {
        return;
    }
    Browser& book = fileBook(*state);
    if (drag->srcIndex >= book.items.size() || !strExtIs(book.items[drag->srcIndex].name.c_str(), ".bhmat")) {
        return;
    }
    char actor[24] = "Scene";
    const uint8_t mark = sceneFramePick(hud->scene, drag->dropX, drag->dropY);
    uint8_t kind = 0;
    if (mark != kSceneActorNone) {
        sceneFrameActorName(hud->scene, mark, actor, sizeof(actor), &kind);
    }
    std::snprintf(dst, cap, "%s -> %s", book.items[drag->srcIndex].name.c_str(), actor);
}

} // namespace burnhope
