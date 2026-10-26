#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "asset/AssimpImporter.hpp"
#include "asset/ProjectDoc.hpp"
#include "platform/Window.hpp"
#include "render/SceneFrame.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {
namespace {

void copyRoot(char* dst, uint32_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    std::snprintf(dst, cap, "%s", src != nullptr ? src : "");
}

void writeEmptyScene(const HudContext& hud) {
    if (hud.projectRoot[0] == '\0') {
        return;
    }
    char path[640]{};
    std::snprintf(path, sizeof(path), "%s/scene.bscene", hud.projectRoot);
    (void)sceneDocWrite(path, nullptr, 0);
}

bool askFolder(HudContext& hud) {
    if (hud.host == nullptr || hud.slot < 0) {
        return false;
    }
    return windowAskFile(hud.host->view[hud.slot].window, nullptr, 0, FileAsk::Folder);
}

bool askSceneFile(HudContext& hud) {
    if (hud.host == nullptr || hud.slot < 0) {
        return false;
    }
    const FileFilter filter{"Scene", "bscene"};
    return windowAskFile(hud.host->view[hud.slot].window, &filter, 1, FileAsk::Open);
}

bool sceneFileEmpty(const char* path) {
    SceneObjectRec recs[4]{};
    uint32_t n = 0;
    return path != nullptr && sceneDocRead(path, recs, 4u, n) && n == 0;
}

void takeProject(Engine& e, const char* folder, uint8_t ask) {
    if (folder == nullptr || folder[0] == '\0') {
        return;
    }
    copyRoot(e.hud.projectRoot, sizeof(e.hud.projectRoot), folder);
    e.hud.matListed = 0;
    char proj[512]{};
    std::snprintf(proj, sizeof(proj), "%s/project.proj", folder);
    ProjectInfo info{};
    if (!projectRead(proj, info)) {
        info.version = kEngineVersion;
        const char* leaf = strPathLeaf(folder);
        std::snprintf(info.name, sizeof(info.name), "%s", leaf[0] != '\0' ? leaf : "Untitled");
        if (!projectWrite(folder, info)) {
            e.hud.projectRoot[0] = '\0';
            return;
        }
    }
    if (ask == 3) {
        (void)sceneFrameSave(e.hud.scene, e.hud.projectRoot);
    } else if (ask == 2) {
        char scenePath[640]{};
        std::snprintf(scenePath, sizeof(scenePath), "%s/scene.bscene", folder);
        std::error_code missing;
        if (!std::filesystem::is_regular_file(scenePath, missing) || sceneFileEmpty(scenePath)) {
            writeEmptyScene(e.hud);
            sceneFrameBlank(e.hud.scene);
        } else {
            (void)sceneFrameLoad(e.hud.scene, e.hud.projectRoot);
        }
    } else {
        writeEmptyScene(e.hud);
        sceneFrameBlank(e.hud.scene);
    }
    e.hud.rootReady = 1;
}

} // namespace

void cmdFile(void* user, uint16_t kind) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    if (kind == 3) {
        std::exit(0);
    }
    if (kind == 2) {
        if (hud->projectRoot[0] == '\0') {
            hud->projectAsk = 3;
            (void)askFolder(*hud);
            return;
        }
        (void)sceneFrameSave(hud->scene, hud->projectRoot);
        return;
    }
    if (kind == 4) {
        hud->projectAsk = 4;
        (void)askSceneFile(*hud);
        return;
    }
    hud->projectAsk = kind == 0 ? 1 : 2;
    (void)askFolder(*hud);
}

void cmdFileNew(void* user, uint16_t) { cmdFile(user, 0); }
void cmdFileOpen(void* user, uint16_t) { cmdFile(user, 1); }
void cmdFileSave(void* user, uint16_t) { cmdFile(user, 2); }
void cmdFileLoad(void* user, uint16_t) { cmdFile(user, 4); }
void cmdFileExit(void* user, uint16_t) { cmdFile(user, 3); }

bool projectGatePoll(Engine& e) {
    if (e.hud.texAsk != 0) {
        char picked[512]{};
        if (!windowTakeFile(picked, static_cast<int>(sizeof(picked)))) {
            return false;
        }
        const uint8_t channel = static_cast<uint8_t>(e.hud.texAsk - 1u);
        e.hud.texAsk = 0;
        if (picked[0] != '\0') {
            hudTakeTexture(e.hud, channel, picked);
        }
        return true;
    }
    if (e.hud.projectAsk == 0) {
        return false;
    }
    char picked[512]{};
    if (!windowTakeFile(picked, static_cast<int>(sizeof(picked)))) {
        return false;
    }
    const uint8_t ask = e.hud.projectAsk;
    e.hud.projectAsk = 0;
    if (ask == 4) {
        if (picked[0] != '\0' && sceneFileEmpty(picked)) {
            sceneFrameBlank(e.hud.scene);
        } else if (picked[0] != '\0') {
            (void)sceneFrameLoad(e.hud.scene, e.hud.projectRoot, picked);
        }
        return true;
    }
    takeProject(e, picked, ask);
    return true;
}

void projectImportModel(HudContext& hud, const char* path) {
    if (hud.scene == nullptr || path == nullptr) {
        return;
    }
    if (!strExtIs(path, ".gltf") && !strExtIs(path, ".glb") && !strExtIs(path, ".fbx") && !strExtIs(path, ".obj")) {
        return;
    }
    char dir[640]{};
    if (hud.projectRoot[0] != '\0') {
        std::snprintf(dir, sizeof(dir), "%s/cache", hud.projectRoot);
    } else {
        std::snprintf(dir, sizeof(dir), "cache");
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char stem[64]{};
    strPathStem(path, stem, sizeof(stem));
    char out[720]{};
    std::snprintf(out, sizeof(out), "%s/%s.bhop", dir, stem[0] != '\0' ? stem : "model");
    spdlog::info("drop {} cache {}", path, out);
    const uint8_t before = sceneFrameActorCount(hud.scene);
    if (!sceneImportFbx(path, out)) {
        spdlog::error("drop import failed {}", path);
        return;
    }
    if (!sceneFrameAppendBhop(hud.scene, out)) {
        spdlog::error("drop append failed {}", out);
        return;
    }
    const uint8_t after = sceneFrameActorCount(hud.scene);
    for (uint8_t i = before; i < after; ++i) {
        spawnAndSelectActor(hud, i);
    }
    spdlog::info("drop appended {} actors {}..{}", out, before, after);
}

} // namespace burnhope
