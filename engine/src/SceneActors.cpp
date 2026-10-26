#include "Str.hpp"
#include "asset/AssimpImporter.hpp"
#include "asset/ProjectDoc.hpp"
#include "gpu_scene/Physics.hpp"
#include "render/SceneFrameImpl.hpp"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {

namespace {

void captureActors(SceneFrameImpl& scene) {
    scene.snapId.clear();
    scene.snapActor.clear();
    scene.snapSize.clear();
    scene.snapBytes.clear();
    ecs_world_t* world = scene.actors.world.c_ptr();
    for (uint8_t actor = 0; actor < scene.actors.count; ++actor) {
        flecs::entity ent = scene.actors.ent[actor];
        if (!ent.is_alive()) {
            continue;
        }
        const ecs_type_t* type = ecs_get_type(world, ent.id());
        if (type == nullptr) {
            continue;
        }
        for (int32_t i = 0; i < type->count; ++i) {
            const ecs_id_t id = type->array[i];
            const ecs_type_info_t* info = ecs_get_type_info(world, id);
            if (info == nullptr || info->size <= 0 || info->size > 512) {
                continue;
            }
            const void* src = ecs_get_id(world, ent.id(), id);
            if (src == nullptr) {
                continue;
            }
            const uint32_t at = static_cast<uint32_t>(scene.snapBytes.size());
            scene.snapBytes.resize(at + static_cast<uint32_t>(info->size));
            std::memcpy(scene.snapBytes.data() + at, src, static_cast<size_t>(info->size));
            scene.snapId.push_back(id);
            scene.snapActor.push_back(actor);
            scene.snapSize.push_back(static_cast<uint16_t>(info->size));
        }
    }
}

void restoreActors(SceneFrameImpl& scene) {
    ecs_world_t* world = scene.actors.world.c_ptr();
    uint32_t at = 0;
    for (size_t i = 0; i < scene.snapId.size(); ++i) {
        const uint16_t actor = scene.snapActor[i];
        const uint16_t size = scene.snapSize[i];
        if (actor >= scene.actors.count || at + size > scene.snapBytes.size()) {
            break;
        }
        flecs::entity ent = scene.actors.ent[actor];
        if (ent.is_alive()) {
            ecs_set_id(world, ent.id(), scene.snapId[i], size, scene.snapBytes.data() + at);
        }
        at += size;
    }
}

} // namespace

uint8_t sceneFrameActorCount(const SceneFrame* scene) {
    return scene != nullptr ? scene->actors.count : 0;
}

void sceneFrameActorName(const SceneFrame* scene, uint8_t index, char* name, uint8_t cap, uint8_t* kind) {
    if (name != nullptr && cap > 0) {
        name[0] = '\0';
    }
    if (kind != nullptr) {
        *kind = 0;
    }
    if (scene == nullptr || index >= scene->actors.count || name == nullptr || cap == 0) {
        return;
    }
    const char* src = actorNameOf(scene->actors, index);
    if (actorXformOf(scene->actors, index) == nullptr) {
        return;
    }
    uint8_t n = 0;
    while (src[n] != '\0' && n + 1 < cap) {
        name[n] = src[n];
        ++n;
    }
    name[n] = '\0';
    if (kind != nullptr) {
        *kind = actorKindOf(scene->actors, index);
    }
}

void sceneFrameActorSelect(SceneFrame* scene, uint8_t index) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.selected = index < scene->actors.count ? index : kSceneActorNone;
    scene->actors.drag = kSceneActorNone;
}

uint8_t sceneFrameActorSelected(const SceneFrame* scene) {
    return scene != nullptr ? scene->actors.selected : kSceneActorNone;
}

uint8_t sceneFrameActorKind(const SceneFrame* scene, uint8_t index) {
    if (scene == nullptr || index >= scene->actors.count) {
        return 0;
    }
    return actorXformOf(scene->actors, index) != nullptr ? actorKindOf(scene->actors, index) : static_cast<uint8_t>(0);
}

void sceneFrameActorGizmo(SceneFrame* scene, uint8_t mode) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.tool = mode > 4 ? 0 : mode;
    scene->actors.gizmo = scene->actors.tool;
    scene->actors.drag = kSceneActorNone;
    sceneActorFitTool(scene->actors);
}

void sceneFrameGizmoSpace(SceneFrame* scene, uint8_t worldSpace) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.space = worldSpace == 0 ? 0 : 1;
    scene->actors.drag = kSceneActorNone;
}

void sceneFrameSnap(SceneFrame* scene, uint8_t channel, uint8_t enabled, float step) {
    if (scene == nullptr || channel > 2) {
        return;
    }
    scene->actors.snapOn[channel] = enabled != 0 ? 1 : 0;
    if (step > 1.0e-5f) {
        scene->actors.snapStep[channel] = step;
    }
}

void sceneFrameGizmoPivot(SceneFrame* scene, uint8_t center) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.pivotSpace = center != 0 ? 1 : 0;
    scene->actors.drag = kSceneActorNone;
}

bool sceneFrameActorPivot(const SceneFrame* scene, uint8_t index, float out[3]) {
    if (scene == nullptr || out == nullptr || index >= scene->actors.count) {
        return false;
    }
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    const SceneActors& world = scene->actors;
    if (world.ent[index].is_alive() && world.ent[index].has<Pivot>()) {
        const Pivot& pivot = world.ent[index].get<Pivot>();
        out[0] = pivot.local[0];
        out[1] = pivot.local[1];
        out[2] = pivot.local[2];
    }
    return true;
}

void sceneFrameActorSetPivot(SceneFrame* scene, uint8_t index, const float local[3]) {
    if (scene == nullptr || local == nullptr) {
        return;
    }
    actorWritePivot(scene->actors, index, local);
}

uint8_t sceneFrameActorDuplicate(SceneFrame* scene) {
    if (scene == nullptr) {
        return kSceneActorNone;
    }
    return sceneActorDuplicate(scene->actors);
}

bool sceneFrameActorDragging(const SceneFrame* scene) {
    return scene != nullptr && scene->actors.drag <= 6;
}

void sceneFrameActorEuler(const SceneFrame* scene, uint8_t index, float& pitch, float& yaw) {
    pitch = 35.0f;
    yaw = 30.0f;
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    const Xform pose = sceneActorXform(scene->actors, index);
    pitch = pose.euler[0];
    yaw = pose.euler[1];
}

void sceneFrameActorSetEuler(SceneFrame* scene, uint8_t index, float pitch, float yaw) {
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    Xform pose = sceneActorXform(scene->actors, index);
    pose.euler[0] = pitch;
    pose.euler[1] = yaw;
    sceneActorWriteXform(scene->actors, index, pose);
}

uint8_t sceneFrameActorSections(const SceneFrame* scene, uint8_t index) {
    uint8_t mask = 0;
    if (scene == nullptr || index >= scene->actors.count) {
        return mask;
    }
    const flecs::entity ent = scene->actors.ent[index];
    if (!ent.is_alive()) {
        return mask;
    }
    if (ent.has<Transform>()) {
        mask |= kSecTransform;
    }
    if (ent.has<Material>()) {
        mask |= kSecMaterial;
    }
    if (ent.has<PostProcess>()) {
        mask |= kSecPost;
    }
    if (ent.has<Atmosphere>()) {
        mask |= kSecAtmo;
    }
    if (ent.has<WeatherObj>()) {
        return kSecWeather;
    }
    if (ent.has<SunObj>()) {
        mask |= kSecSun;
    }
    return mask;
}

bool sceneFrameActorPose(const SceneFrame* scene, uint8_t index, ActorPose& out) {
    if (scene == nullptr || index >= scene->actors.count) {
        return false;
    }
    const Xform pose = sceneActorXform(scene->actors, index);
    out.pos[0] = pose.pos[0];
    out.pos[1] = pose.pos[1];
    out.pos[2] = pose.pos[2];
    out.euler[0] = pose.euler[0];
    out.euler[1] = pose.euler[1];
    out.euler[2] = pose.euler[2];
    out.scale[0] = pose.scale[0];
    out.scale[1] = pose.scale[1];
    out.scale[2] = pose.scale[2];
    out.isStatic = 0;
    const flecs::entity ent = scene->actors.ent[index];
    if (ent.is_alive() && ent.has<Transform>()) {
        out.isStatic = ent.get<Transform>().isStatic;
    }
    return true;
}

void sceneFrameActorSetPose(SceneFrame* scene, uint8_t index, const ActorPose& pose) {
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    Xform next = sceneActorXform(scene->actors, index);
    next.pos[0] = pose.pos[0];
    next.pos[1] = pose.pos[1];
    next.pos[2] = pose.pos[2];
    next.euler[0] = pose.euler[0];
    next.euler[1] = pose.euler[1];
    next.euler[2] = pose.euler[2];
    next.scale[0] = pose.scale[0] < 0.05f ? 0.05f : pose.scale[0];
    next.scale[1] = pose.scale[1] < 0.05f ? 0.05f : pose.scale[1];
    next.scale[2] = pose.scale[2] < 0.05f ? 0.05f : pose.scale[2];
    sceneActorWriteXform(scene->actors, index, next);
    flecs::entity ent = scene->actors.ent[index];
    if (ent.is_alive() && ent.has<Transform>()) {
        ent.get_mut<Transform>().isStatic = pose.isStatic;
    }
}

bool sceneFrameActorPbr(const SceneFrame* scene, uint8_t index, ActorPbr& out) {
    if (scene == nullptr || index >= scene->actors.count) {
        return false;
    }
    flecs::entity ent = scene->actors.ent[index];
    if (!ent.is_alive() || !ent.has<Material>()) {
        return false;
    }
    const Material& mat = ent.get<Material>();
    out.base[0] = mat.base[0];
    out.base[1] = mat.base[1];
    out.base[2] = mat.base[2];
    out.roughness = mat.roughness;
    out.metallic = mat.metallic;
    out.emissive = mat.emissive;
    return true;
}

void sceneFrameActorSetPbr(SceneFrame* scene, uint8_t index, const ActorPbr& pbr) {
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    flecs::entity ent = scene->actors.ent[index];
    if (!ent.is_alive()) {
        return;
    }
    Material mat{};
    if (ent.has<Material>()) {
        mat = ent.get<Material>();
    }
    mat.base[0] = pbr.base[0];
    mat.base[1] = pbr.base[1];
    mat.base[2] = pbr.base[2];
    mat.base[3] = 1.0f;
    mat.roughness = pbr.roughness;
    mat.metallic = pbr.metallic;
    mat.emissive = pbr.emissive;
    ent.set<Material>(mat);
}

void sceneFramePlay(SceneFrame* scene) {
    if (scene == nullptr || scene->sim != 0) {
        return;
    }
    captureActors(*scene);
    scene->sim = 1;
}

void sceneFrameStop(SceneFrame* scene) {
    if (scene == nullptr || scene->sim == 0) {
        return;
    }
    restoreActors(*scene);
    scene->sim = 1;
    if (scene->physics != nullptr) {
        const uint32_t n = physicsBodyCount(scene->physics);
        PhysicsPose poses[kPropCap]{};
        const uint32_t count = n < kPropCap ? n : kPropCap;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            const Xform pose = sceneActorXform(scene->actors, actor);
            poses[i].pos[0] = pose.pos[0];
            poses[i].pos[1] = pose.pos[1];
            poses[i].pos[2] = pose.pos[2];
            poses[i].euler[0] = pose.euler[0];
            poses[i].euler[1] = pose.euler[1];
            poses[i].euler[2] = pose.euler[2];
            poses[i].scale[0] = pose.scale[0];
            poses[i].scale[1] = pose.scale[1];
            poses[i].scale[2] = pose.scale[2];
        }
        physicsStep(scene->physics, 0.0f, kSceneActorNone, poses, count);
    }
    scene->sim = 0;
}

void sceneFramePause(SceneFrame* scene, uint8_t paused) {
    if (scene == nullptr || scene->sim == 0) {
        return;
    }
    scene->sim = paused != 0 ? 2 : 1;
}

uint8_t sceneFramePick(const SceneFrame* scene, float x, float y) {
    if (scene == nullptr) {
        return kSceneActorNone;
    }
    uint8_t axis = kSceneActorNone;
    return sceneActorHit(scene->actors, x, y, axis);
}

void sceneFrameMark(SceneFrame* scene, uint8_t actor) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.mark = actor;
}

namespace {

constexpr uint32_t kSceneRecCap = 96;

void sceneRecPath(char* dst, size_t cap, const char* root) {
    std::snprintf(dst, cap, "%s/scene.bscene", root != nullptr ? root : "");
}

} // namespace

bool sceneFrameSave(const SceneFrame* scene, const char* projectRoot) {
    if (scene == nullptr || projectRoot == nullptr || projectRoot[0] == '\0') {
        return false;
    }
    SceneObjectRec recs[kSceneRecCap]{};
    const uint8_t count = sceneFrameActorCount(scene);
    uint32_t n = 0;
    bool wrote = true;
    for (uint8_t i = 0; i < count && n < kSceneRecCap; ++i) {
        SceneObjectRec& rec = recs[n];
        uint8_t kind = 0;
        sceneFrameActorName(scene, i, rec.name, sizeof(rec.name), &kind);
        rec.kind = kind;
        ActorPose pose{};
        if (sceneFrameActorPose(scene, i, pose)) {
            std::memcpy(rec.pos, pose.pos, sizeof(rec.pos));
            std::memcpy(rec.euler, pose.euler, sizeof(rec.euler));
            std::memcpy(rec.scale, pose.scale, sizeof(rec.scale));
        }
        ActorPbr pbr{};
        if (sceneFrameActorPbr(scene, i, pbr)) {
            std::memcpy(rec.base, pbr.base, sizeof(rec.base));
            rec.roughness = pbr.roughness;
            rec.metallic = pbr.metallic;
            rec.emissive = pbr.emissive;
            MaterialInfo mat{};
            mat.version = kEngineVersion;
            std::memcpy(mat.base, pbr.base, sizeof(mat.base));
            mat.roughness = pbr.roughness;
            mat.metallic = pbr.metallic;
            mat.emissive = pbr.emissive;
            char matPath[800]{};
            std::snprintf(matPath, sizeof(matPath), "%s/materials", projectRoot);
            std::error_code ec;
            std::filesystem::create_directories(matPath, ec);
            std::snprintf(rec.material, sizeof(rec.material), "materials/mat_%u.bhmat", i);
            std::snprintf(matPath, sizeof(matPath), "%s/%s", projectRoot, rec.material);
            wrote = materialDocWrite(matPath, mat) && wrote;
        }
        n += 1u;
    }
    char path[640]{};
    sceneRecPath(path, sizeof(path), projectRoot);
    wrote = sceneDocWrite(path, recs, n) && wrote;
    if (!wrote) {
        spdlog::warn("scene save incomplete {}", path);
    }
    return wrote;
}

bool sceneFrameLoad(SceneFrame* scene, const char* projectRoot, const char* file) {
    if (scene == nullptr) {
        return false;
    }
    char path[640]{};
    if (file != nullptr && file[0] != '\0') {
        std::snprintf(path, sizeof(path), "%s", file);
    } else if (projectRoot == nullptr || projectRoot[0] == '\0') {
        return false;
    } else {
        sceneRecPath(path, sizeof(path), projectRoot);
    }
    SceneObjectRec recs[kSceneRecCap]{};
    uint32_t n = 0;
    if (!sceneDocRead(path, recs, kSceneRecCap, n)) {
        spdlog::warn("scene load failed {}", path);
        return false;
    }
    const uint8_t count = sceneFrameActorCount(scene);
    for (uint32_t i = 0; i < n; ++i) {
        const SceneObjectRec& rec = recs[i];
        for (uint8_t a = 0; a < count; ++a) {
            char name[24]{};
            uint8_t kind = 0;
            sceneFrameActorName(scene, a, name, sizeof(name), &kind);
            if (std::strcmp(name, rec.name) != 0) {
                continue;
            }
            ActorPose pose{};
            std::memcpy(pose.pos, rec.pos, sizeof(pose.pos));
            std::memcpy(pose.euler, rec.euler, sizeof(pose.euler));
            for (uint8_t axis = 0; axis < 3; ++axis) {
                pose.scale[axis] = rec.scale[axis] < 0.05f ? 1.0f : rec.scale[axis];
            }
            sceneFrameActorSetPose(scene, a, pose);
            ActorPbr pbr{};
            std::memcpy(pbr.base, rec.base, sizeof(pbr.base));
            pbr.roughness = rec.roughness;
            pbr.metallic = rec.metallic;
            pbr.emissive = rec.emissive;
            if (rec.material[0] != '\0') {
                char matPath[800]{};
                std::snprintf(matPath, sizeof(matPath), "%s/%s", projectRoot != nullptr ? projectRoot : "", rec.material);
                MaterialInfo mat{};
                if (materialDocRead(matPath, mat)) {
                    std::memcpy(pbr.base, mat.base, sizeof(pbr.base));
                    pbr.roughness = mat.roughness * mat.ormGain;
                    pbr.metallic = mat.metallic * mat.ormGain;
                    pbr.emissive = mat.emissive * mat.emissiveGain;
                }
            }
            sceneFrameActorSetPbr(scene, a, pbr);
            break;
        }
    }
    return true;
}

bool sceneFrameImportModel(SceneFrame* scene, const char* projectRoot, const char* path, uint8_t& outFirstActor, uint8_t& outAfterActor) {
    outFirstActor = 0;
    outAfterActor = 0;
    if (scene == nullptr || path == nullptr) {
        return false;
    }
    if (!strExtIs(path, ".gltf") && !strExtIs(path, ".glb") && !strExtIs(path, ".fbx") && !strExtIs(path, ".obj")) {
        return false;
    }
    char dir[640]{};
    if (projectRoot != nullptr && projectRoot[0] != '\0') {
        std::snprintf(dir, sizeof(dir), "%s/cache", projectRoot);
    } else {
        std::snprintf(dir, sizeof(dir), "cache");
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char stem[64]{};
    strPathStem(path, stem, sizeof(stem));
    char out[720]{};
    std::snprintf(out, sizeof(out), "%s/%s.bhop", dir, stem[0] != '\0' ? stem : "model");
    spdlog::info("import {} cache {}", path, out);
    const uint8_t before = sceneFrameActorCount(scene);
    if (!sceneImportFbx(path, out)) {
        spdlog::error("import failed {}", path);
        return false;
    }
    if (!sceneFrameAppendBhop(scene, out)) {
        spdlog::error("append failed {}", out);
        return false;
    }
    outFirstActor = before;
    outAfterActor = sceneFrameActorCount(scene);
    spdlog::info("import appended {} actors {}..{}", out, before, outAfterActor);
    return true;
}

} // namespace burnhope
