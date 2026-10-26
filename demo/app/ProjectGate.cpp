#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "asset/ProjectDoc.hpp"
#include "platform/Window.hpp"
#include "render/SceneFrame.hpp"
#include "Str.hpp"

#include <cstdio>
#include <ctime>
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

void makeProjectFolders(const char* folder) {
    std::error_code ec;
    for (const char* sub : {"assets", "cache", "materials"}) {
        std::filesystem::create_directories(std::filesystem::path(folder) / sub, ec);
    }
}

void remember(HudContext& hud) {
    recentTouch(hud.recent, hud.projectRoot, static_cast<int64_t>(std::time(nullptr)));
    char file[512]{};
    if (recentFile(file, sizeof(file))) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
        (void)recentSave(hud.recent, file);
    }
    hud.launcherDirty = 1;
}

void takeProject(HudContext& hud, const char* folder, uint8_t ask) {
    if (folder == nullptr || folder[0] == '\0') {
        return;
    }
    copyRoot(hud.projectRoot, sizeof(hud.projectRoot), folder);
    hud.matListed = 0;
    char proj[512]{};
    std::snprintf(proj, sizeof(proj), "%s/project.proj", folder);
    ProjectInfo info{};
    if (!projectRead(proj, info)) {
        info.version = kEngineVersion;
        const char* leaf = strPathLeaf(folder);
        std::snprintf(info.name, sizeof(info.name), "%s", leaf[0] != '\0' ? leaf : "Untitled");
        if (!projectWrite(folder, info)) {
            hud.projectRoot[0] = '\0';
            return;
        }
    }
    if (ask == 1) {
        makeProjectFolders(folder);
    }
    if (ask == 3) {
        (void)sceneFrameSave(hud.scene, hud.projectRoot);
    } else if (ask == 2) {
        char scenePath[640]{};
        std::snprintf(scenePath, sizeof(scenePath), "%s/scene.bscene", folder);
        std::error_code missing;
        if (!std::filesystem::is_regular_file(scenePath, missing) || sceneFileEmpty(scenePath)) {
            writeEmptyScene(hud);
            sceneFrameBlank(hud.scene);
        } else {
            (void)sceneFrameLoad(hud.scene, hud.projectRoot);
        }
    } else {
        writeEmptyScene(hud);
        sceneFrameBlank(hud.scene);
    }
    hud.rootReady = 1;
    remember(hud);
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
    projectTake(e.hud, picked, ask);
    return true;
}

void projectTake(HudContext& hud, const char* folder, uint8_t ask) {
    takeProject(hud, folder, ask);
}

void projectImportModel(HudContext& hud, const char* path) {
    uint8_t first = 0;
    uint8_t after = 0;
    if (!sceneFrameImportModel(hud.scene, hud.projectRoot, path, first, after)) {
        return;
    }
    for (uint8_t i = first; i < after; ++i) {
        spawnAndSelectActor(hud, i);
    }
}

} // namespace burnhope
