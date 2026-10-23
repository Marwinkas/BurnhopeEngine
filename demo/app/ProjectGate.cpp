#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "asset/AssimpImporter.hpp"
#include "asset/ProjectDoc.hpp"
#include "files/Browser.hpp"
#include "platform/Window.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Canvas.hpp"
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

void saveScene(HudContext& hud) {
    if (hud.scene == nullptr || hud.projectRoot[0] == '\0') {
        return;
    }
    SceneObjectRec recs[96]{};
    const uint8_t count = sceneFrameActorCount(hud.scene);
    uint32_t n = 0;
    for (uint8_t i = 0; i < count && n < 96u; ++i) {
        SceneObjectRec& rec = recs[n];
        uint8_t kind = 0;
        sceneFrameActorName(hud.scene, i, rec.name, sizeof(rec.name), &kind);
        rec.kind = kind;
        ActorPose pose{};
        if (sceneFrameActorPose(hud.scene, i, pose)) {
            rec.pos[0] = pose.pos[0];
            rec.pos[1] = pose.pos[1];
            rec.pos[2] = pose.pos[2];
            rec.euler[0] = pose.euler[0];
            rec.euler[1] = pose.euler[1];
            rec.euler[2] = pose.euler[2];
            rec.scale[0] = pose.scale[0];
            rec.scale[1] = pose.scale[1];
            rec.scale[2] = pose.scale[2];
        }
        ActorPbr pbr{};
        if (sceneFrameActorPbr(hud.scene, i, pbr)) {
            rec.base[0] = pbr.base[0];
            rec.base[1] = pbr.base[1];
            rec.base[2] = pbr.base[2];
            rec.roughness = pbr.roughness;
            rec.metallic = pbr.metallic;
            rec.emissive = pbr.emissive;
            MaterialInfo mat{};
            mat.version = kEngineVersion;
            mat.base[0] = pbr.base[0];
            mat.base[1] = pbr.base[1];
            mat.base[2] = pbr.base[2];
            mat.roughness = pbr.roughness;
            mat.metallic = pbr.metallic;
            mat.emissive = pbr.emissive;
            char matPath[800]{};
            std::snprintf(matPath, sizeof(matPath), "%s/materials", hud.projectRoot);
            std::error_code ec;
            std::filesystem::create_directories(matPath, ec);
            std::snprintf(rec.material, sizeof(rec.material), "materials/mat_%u.bhmat", i);
            std::snprintf(matPath, sizeof(matPath), "%s/%s", hud.projectRoot, rec.material);
            (void)materialDocWrite(matPath, mat);
        }
        n += 1u;
    }
    char path[640]{};
    std::snprintf(path, sizeof(path), "%s/scene.bscene", hud.projectRoot);
    (void)sceneDocWrite(path, recs, n);
}

void loadScene(HudContext& hud, const char* file = nullptr) {
    if (hud.scene == nullptr) {
        return;
    }
    char path[640]{};
    if (file != nullptr && file[0] != '\0') {
        std::snprintf(path, sizeof(path), "%s", file);
    } else if (hud.projectRoot[0] == '\0') {
        return;
    } else {
        std::snprintf(path, sizeof(path), "%s/scene.bscene", hud.projectRoot);
    }
    SceneObjectRec recs[96]{};
    uint32_t n = 0;
    if (!sceneDocRead(path, recs, 96u, n)) {
        return;
    }
    const uint8_t count = sceneFrameActorCount(hud.scene);
    for (uint32_t i = 0; i < n; ++i) {
        for (uint8_t a = 0; a < count; ++a) {
            char name[24]{};
            uint8_t kind = 0;
            sceneFrameActorName(hud.scene, a, name, sizeof(name), &kind);
            if (std::strcmp(name, recs[i].name) != 0) {
                continue;
            }
            ActorPose pose{};
            pose.pos[0] = recs[i].pos[0];
            pose.pos[1] = recs[i].pos[1];
            pose.pos[2] = recs[i].pos[2];
            pose.euler[0] = recs[i].euler[0];
            pose.euler[1] = recs[i].euler[1];
            pose.euler[2] = recs[i].euler[2];
            pose.scale[0] = recs[i].scale[0] < 0.05f ? 1.0f : recs[i].scale[0];
            pose.scale[1] = recs[i].scale[1] < 0.05f ? 1.0f : recs[i].scale[1];
            pose.scale[2] = recs[i].scale[2] < 0.05f ? 1.0f : recs[i].scale[2];
            sceneFrameActorSetPose(hud.scene, a, pose);
            ActorPbr pbr{};
            pbr.base[0] = recs[i].base[0];
            pbr.base[1] = recs[i].base[1];
            pbr.base[2] = recs[i].base[2];
            pbr.roughness = recs[i].roughness;
            pbr.metallic = recs[i].metallic;
            pbr.emissive = recs[i].emissive;
            if (recs[i].material[0] != '\0') {
                char matPath[800]{};
                std::snprintf(matPath, sizeof(matPath), "%s/%s", hud.projectRoot, recs[i].material);
                MaterialInfo mat{};
                if (materialDocRead(matPath, mat)) {
                    pbr.base[0] = mat.base[0];
                    pbr.base[1] = mat.base[1];
                    pbr.base[2] = mat.base[2];
                    pbr.roughness = mat.roughness * mat.ormGain;
                    pbr.metallic = mat.metallic * mat.ormGain;
                    pbr.emissive = mat.emissive * mat.emissiveGain;
                }
            }
            sceneFrameActorSetPbr(hud.scene, a, pbr);
            break;
        }
    }
}

void takeProject(Engine& e, const char* folder, uint8_t ask) {
    if (folder == nullptr || folder[0] == '\0') {
        return;
    }
    copyRoot(e.hud.projectRoot, sizeof(e.hud.projectRoot), folder);
    char proj[512]{};
    std::snprintf(proj, sizeof(proj), "%s/project.proj", folder);
    ProjectInfo info{};
    if (!projectRead(proj, info)) {
        info.version = kEngineVersion;
        const char* slash = std::strrchr(folder, '/');
        const char* leaf = slash != nullptr && slash[1] != '\0' ? slash + 1 : folder;
        std::snprintf(info.name, sizeof(info.name), "%s", leaf[0] != '\0' ? leaf : "Untitled");
        if (!projectWrite(folder, info)) {
            e.hud.projectRoot[0] = '\0';
            return;
        }
    }
    if (ask == 3) {
        saveScene(e.hud);
    } else if (ask == 2) {
        char scenePath[640]{};
        std::snprintf(scenePath, sizeof(scenePath), "%s/scene.bscene", folder);
        std::error_code missing;
        if (!std::filesystem::is_regular_file(scenePath, missing) || sceneFileEmpty(scenePath)) {
            writeEmptyScene(e.hud);
            sceneFrameBlank(e.hud.scene);
        } else {
            loadScene(e.hud);
        }
    } else {
        writeEmptyScene(e.hud);
        sceneFrameBlank(e.hud.scene);
    }
    if (e.hud.slot >= 0 && e.hud.projectRoot[0] != '\0') {
        Canvas* canvas = hostCanvas(e.host, e.hud.slot);
        if (canvas != nullptr && canvas->state != nullptr) {
            filesRoot(*canvas->state, e.hud.projectRoot);
        }
    }
}

} // namespace

void projectOnFile(FileAct* hit) {
    if (hit == nullptr || hit->hud == nullptr) {
        return;
    }
    if (hit->kind == 3) {
        std::exit(0);
    }
    if (hit->kind == 2) {
        if (hit->hud->projectRoot[0] == '\0') {
            hit->hud->projectAsk = 3;
            (void)askFolder(*hit->hud);
            return;
        }
        saveScene(*hit->hud);
        return;
    }
    if (hit->kind == 4) {
        hit->hud->projectAsk = 4;
        (void)askSceneFile(*hit->hud);
        return;
    }
    hit->hud->projectAsk = hit->kind == 0 ? 1 : 2;
    (void)askFolder(*hit->hud);
}

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
            loadScene(e.hud, picked);
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
    const char* leaf = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            leaf = p + 1;
        }
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
    uint32_t stemLen = 0;
    for (const char* p = leaf; *p != '\0' && *p != '.' && stemLen + 1u < sizeof(stem); ++p) {
        stem[stemLen++] = *p;
    }
    stem[stemLen] = '\0';
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
