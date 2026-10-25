#include "audio/Audio.hpp"
#include "Str.hpp"
#include "platform/PlatformUtil.hpp"
#include "gpu_scene/Physics.hpp"
#include "render/Weather.hpp"
#include "render/SceneFrameImpl.hpp"
#include "gpu_scene/InstanceCull.hpp"
#include "gpu_scene/PointShadow.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace burnhope {

namespace {

void pushBlob(SceneFrameImpl& s, const MeshletBlob& blob, float x, float y, float z) {
    if (s.instanceCount >= kSceneInstances || s.vertCount + blob.vertexCount > kSceneVerts) {
        return;
    }
    const uint32_t base = s.vertCount;
    const uint32_t indexAt = s.indexCount;
    for (uint32_t i = 0; i < blob.vertexCount; ++i) {
        GpuVertex& dst = s.cpuVerts[s.vertCount];
        dst = {};
        dst.px = blob.vertices[i].pos[0];
        dst.py = blob.vertices[i].pos[1];
        dst.pz = blob.vertices[i].pos[2];
        dst.ny = 1.0f;
        s.vertCount += 1;
    }
    uint32_t tris = 0;
    for (uint32_t t = 0; t < blob.triangleCount && s.indexCount + 3 <= kSceneTris * 3; ++t) {
        s.cpuIndex[s.indexCount + 0] = blob.indices[t * 3 + 0];
        s.cpuIndex[s.indexCount + 1] = blob.indices[t * 3 + 1];
        s.cpuIndex[s.indexCount + 2] = blob.indices[t * 3 + 2];
        s.indexCount += 3;
        tris += 1;
    }
    GpuMeshlet& meshlet = s.cpuMeshlets[s.instanceCount];
    meshlet.vertexOffset = base;
    meshlet.indexOffset = indexAt;
    meshlet.vertexCount = blob.vertexCount;
    meshlet.triangleCount = tris;
    const float radius = blob.meshlets[0].radius > 0.0f ? blob.meshlets[0].radius : 1.0f;
    instanceSet(s.cpuInstances[s.instanceCount], x, y, z, radius, blob.materialId, s.instanceCount);
    s.instanceCount += 1;
}

void writeGroupSpheres(const GpuInstance* instances, GpuMeshlet* meshlets, uint32_t begin, uint32_t count, uint32_t meshletCount) {
    struct Box {
        uint32_t group = 0;
        float minX = 1.0e30f;
        float minY = 1.0e30f;
        float minZ = 1.0e30f;
        float maxX = -1.0e30f;
        float maxY = -1.0e30f;
        float maxZ = -1.0e30f;
    };
    std::vector<Box> boxes;
    boxes.reserve(64);
    for (uint32_t n = 0; n < count; ++n) {
        const GpuInstance& inst = instances[begin + n];
        if (inst.meshId >= meshletCount) {
            continue;
        }
        const uint32_t group = inst.pad[1] & 0x0fffffffu;
        Box* box = nullptr;
        for (Box& item : boxes) {
            if (item.group == group) {
                box = &item;
                break;
            }
        }
        if (box == nullptr) {
            boxes.push_back({});
            box = &boxes.back();
            box->group = group;
            box->minX = box->minY = box->minZ = 1.0e30f;
            box->maxX = box->maxY = box->maxZ = -1.0e30f;
        }
        const float x = inst.sphere[0];
        const float y = inst.sphere[1];
        const float z = inst.sphere[2];
        const float r = inst.sphere[3];
        box->minX = std::min(box->minX, x - r);
        box->minY = std::min(box->minY, y - r);
        box->minZ = std::min(box->minZ, z - r);
        box->maxX = std::max(box->maxX, x + r);
        box->maxY = std::max(box->maxY, y + r);
        box->maxZ = std::max(box->maxZ, z + r);
    }
    for (uint32_t n = 0; n < count; ++n) {
        const GpuInstance& inst = instances[begin + n];
        if (inst.meshId >= meshletCount) {
            continue;
        }
        const uint32_t group = inst.pad[1] & 0x0fffffffu;
        const Box* box = nullptr;
        for (const Box& item : boxes) {
            if (item.group == group) {
                box = &item;
                break;
            }
        }
        if (box == nullptr) {
            continue;
        }
        GpuMeshlet& mesh = meshlets[inst.meshId];
        mesh.groupCenter[0] = (box->minX + box->maxX) * 0.5f;
        mesh.groupCenter[1] = (box->minY + box->maxY) * 0.5f;
        mesh.groupCenter[2] = (box->minZ + box->maxZ) * 0.5f;
        const float dx = box->maxX - mesh.groupCenter[0];
        const float dy = box->maxY - mesh.groupCenter[1];
        const float dz = box->maxZ - mesh.groupCenter[2];
        mesh.groupRadius = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

void adoptLoadedModel(SceneFrame& scene, const char* path, float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    if (scene.instSrc == nullptr || scene.instanceCount == 0) {
        return;
    }
    const char* leaf = path != nullptr ? path : "Model";
    for (const char* p = leaf; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            leaf = p + 1;
        }
    }
    char name[24]{};
    uint32_t n = 0;
    for (const char* p = leaf; *p != '\0' && *p != '.' && n + 1u < sizeof(name); ++p) {
        name[n++] = *p;
    }
    if (n == 0) {
        name[0] = 'M';
        name[1] = '\0';
    }
    const float pos[3] = {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
    const uint8_t actor = sceneActorAdd(scene.actors, SceneActorKind::Prop, pos, name);
    if (actor == kSceneActorNone || actor >= scene.actors.count) {
        spdlog::error("actor for {} was not created", name);
        return;
    }
    flecs::entity ent = scene.actors.ent[actor];
    ActorBounds box{};
    box.half[0] = std::max((maxX - minX) * 0.5f, 0.01f);
    box.half[1] = std::max((maxY - minY) * 0.5f, 0.01f);
    box.half[2] = std::max((maxZ - minZ) * 0.5f, 0.01f);
    ent.set<ActorBounds>(box);
    const GpuInstance& inst = scene.instSrc[0];
    MeshBind bind{};
    bind.material = inst.materialId;
    bind.instance = 0;
    bind.anchor[0] = pos[0] - inst.world[3];
    bind.anchor[1] = pos[1] - inst.world[7];
    bind.anchor[2] = pos[2] - inst.world[11];
    ent.set<MeshBind>(bind);
    MeshSpan span{};
    span.begin = 0;
    span.count = scene.instanceCount;
    ent.set<MeshSpan>(span);
    if (scene.materialSrc != nullptr && inst.materialId < scene.materialCount) {
        const MaterialGpuData& src = scene.materialSrc[inst.materialId];
        Material pbr{};
        pbr.base[0] = src.baseColor[0];
        pbr.base[1] = src.baseColor[1];
        pbr.base[2] = src.baseColor[2];
        pbr.base[3] = src.baseColor[3];
        pbr.roughness = src.roughness;
        pbr.metallic = src.metallic;
        pbr.emissive = src.emissiveIntensity;
        ent.set<Material>(pbr);
    }
    scene.materialCap = scene.materialCount + kEditorSpare;
    spdlog::info("actor {} index {} inst {} bounds {:.3f} {:.3f} {:.3f}", name, actor, scene.instanceCount, box.half[0], box.half[1], box.half[2]);
}

uint32_t remapTex(uint32_t index, uint32_t texBase) {
    return index == 0u ? 0u : texBase + index - 1u;
}

void syncMeshActors(SceneFrameImpl& scene) {
    if (scene.instSrc == nullptr || scene.instanceCount == 0) {
        return;
    }
    bool moved = false;
    for (uint8_t n = 0; n < scene.actors.count; ++n) {
        flecs::entity ent = scene.actors.ent[n];
        if (!ent.is_alive() || !ent.has<MeshBind>()) {
            continue;
        }
        const MeshBind bind = ent.get<MeshBind>();
        if (bind.instance >= scene.instanceCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene.actors, n);
        const GpuInstance& inst = scene.instSrc[bind.instance];
        const float wantX = pose.pos[0] - bind.anchor[0];
        const float wantY = pose.pos[1] - bind.anchor[1];
        const float wantZ = pose.pos[2] - bind.anchor[2];
        const float dx = wantX - inst.world[3];
        const float dy = wantY - inst.world[7];
        const float dz = wantZ - inst.world[11];
        if (dx * dx + dy * dy + dz * dz > 1.0e-4f) {
            moved = true;
            break;
        }
    }
    if (!moved) {
        return;
    }
    if (scene.instSrc != scene.ownedInst.data()) {
        scene.ownedInst.assign(scene.instSrc, scene.instSrc + scene.instanceCount);
        scene.instSrc = scene.ownedInst.data();
    }
    auto* gpu = scene.instances.mapped != nullptr ? static_cast<GpuInstance*>(scene.instances.mapped) : nullptr;
    for (uint8_t n = 0; n < scene.actors.count; ++n) {
        flecs::entity ent = scene.actors.ent[n];
        if (!ent.is_alive() || !ent.has<MeshBind>()) {
            continue;
        }
        const MeshBind bind = ent.get<MeshBind>();
        if (bind.instance >= scene.instanceCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene.actors, n);
        GpuInstance& inst = scene.ownedInst[bind.instance];
        const float wantX = pose.pos[0] - bind.anchor[0];
        const float wantY = pose.pos[1] - bind.anchor[1];
        const float wantZ = pose.pos[2] - bind.anchor[2];
        const float dx = wantX - inst.world[3];
        const float dy = wantY - inst.world[7];
        const float dz = wantZ - inst.world[11];
        if (ent.has<MeshSpan>()) {
            const MeshSpan span = ent.get<MeshSpan>();
            const uint32_t end = span.begin + span.count;
            for (uint32_t i = span.begin; i < end && i < scene.instanceCount; ++i) {
                GpuInstance& part = scene.ownedInst[i];
                part.sphere[0] += dx;
                part.sphere[1] += dy;
                part.sphere[2] += dz;
                part.world[3] += dx;
                part.world[7] += dy;
                part.world[11] += dz;
                if (gpu != nullptr) {
                    gpu[i] = part;
                }
            }
            continue;
        }
        inst.sphere[0] += dx;
        inst.sphere[1] += dy;
        inst.sphere[2] += dz;
        inst.world[3] = wantX;
        inst.world[7] = wantY;
        inst.world[11] = wantZ;
        if (gpu != nullptr) {
            gpu[bind.instance] = inst;
        }
    }
}

} // namespace

void placeActors(SceneFrameImpl& scene) {
    sceneActorsClear(scene.actors);
    const float post[3] = {scene.fly.eye[0], scene.fly.eye[1] + 2.0f, scene.fly.eye[2] - 6.0f};
    sceneActorAdd(scene.actors, SceneActorKind::Post, post, "Post");
    const float climate[3] = {post[0] + 3.0f, post[1], post[2]};
    const uint8_t weather = sceneActorAdd(scene.actors, SceneActorKind::Weather, climate, "Weather");
    scene.actors.selected = weather;
    if (weather < scene.actors.count) {
        Xform pose = sceneActorXform(scene.actors, weather);
        pose.euler[0] = 35.0f;
        pose.euler[1] = 30.0f;
        sceneActorWriteXform(scene.actors, weather, pose);
        scene.weatherEnt = scene.actors.ent[weather];
        scene.weatherEnt.set<Weather>(Weather{});
        sceneActorFitTool(scene.actors);
    }
}

void armEditorRoom(SceneFrameImpl& s) {
    const uint32_t vRoom = kEditorSpare * 24u;
    const uint32_t iRoom = kEditorSpare * 36u;
    if (!s.ownedVert.empty() && s.vertSrc == s.ownedVert.data()) {
        s.ownedVert.reserve(s.ownedVert.size() + vRoom);
        s.ownedIndex.reserve(s.ownedIndex.size() + iRoom);
        s.ownedInst.reserve(s.ownedInst.size() + kEditorSpare);
        s.ownedMesh.reserve(s.ownedMesh.size() + kEditorSpare);
        s.ownedMat.reserve(s.ownedMat.size() + kEditorSpare);
        s.vertSrc = s.ownedVert.data();
        s.indexSrc = s.ownedIndex.data();
        s.instSrc = s.ownedInst.data();
        s.meshletSrc = s.ownedMesh.data();
        s.materialSrc = s.ownedMat.data();
        s.vertCap = s.vertCount + vRoom;
        s.indexCap = s.indexCount + iRoom;
        s.instanceCap = s.instanceCount + kEditorSpare;
        s.materialCap = s.materialCount + kEditorSpare;
    } else if (s.vertSrc == s.cpuVerts) {
        s.vertCap = kSceneVerts;
        s.indexCap = kSceneTris * 3u;
        s.instanceCap = kSceneInstances;
        s.materialCap = s.materialCount + kEditorSpare;
    }
    if (s.instanceCap > s.visibleCap) {
        s.visibleCpu.resize(s.instanceCap, 0u);
        s.visibleCap = s.instanceCap;
    }
}

SceneFrame* sceneFrameCreate() {
    SceneFrame* scene = new SceneFrame{};
    scene->block.scalars[kScGtaoRadius] = 0.5f;
    scene->block.scalars[kScGtaoSteps] = 3.0f;
    scene->block.scalars[kScGtaoSlices] = 3.0f;
    scene->block.scalars[kScGtaoPower] = 2.2f;
    scene->block.scalars[kScGtaoFalloff] = 0.615f;
    scene->block.scalars[kScGtaoDenoise] = 1.0f;
    scene->block.scalars[kScSsrCutoff] = 0.82f;
    scene->block.scalars[kScSsrSteps] = 64.0f;
    scene->block.scalars[kScSsrStride] = 1.0f;
    scene->block.scalars[kScSsrThick] = 0.02f;
    scene->block.scalars[kScSsrMetal] = 0.35f;
    scene->block.scalars[kScSsrRt] = 0.45f;
    scene->block.scalars[kScSsrEdge] = 24.0f;
    scene->block.scalars[kScSsrMax] = 80.0f;
    scene->block.scalars[kScSsrBias] = 0.15f;
    scene->block.scalars[kScFogDensity] = 0.06f;
    scene->block.scalars[kScFogHeight] = 0.0f;
    scene->block.scalars[kScFogScatter] = 40.0f;
    scene->block.scalars[kScFogFalloff] = 0.08f;
    scene->block.scalars[kScFogR] = 0.62f;
    scene->block.scalars[kScFogG] = 0.72f;
    scene->block.scalars[kScFogB] = 0.86f;
    scene->block.scalars[kScFogLum] = 1.4f;
    scene->block.scalars[kScFogStart] = 0.0f;
    scene->block.scalars[kScFogOpacity] = 0.92f;
    scene->block.scalars[kScFogInStart] = 0.0f;
    scene->audio = audioCreate();
    testSceneBuild(scene->world);
    pointShadowAssign(scene->world.lights, scene->world.lightCount);
    scene->fly.eye[0] = scene->world.eye[0];
    scene->fly.eye[1] = scene->world.eye[1];
    scene->fly.eye[2] = scene->world.eye[2];
    placeActors(*scene);
    pushBlob(*scene, scene->world.floor, scene->world.objects[0].x, scene->world.objects[0].y, scene->world.objects[0].z);
    pushBlob(*scene, scene->world.cubes[0], scene->world.objects[1].x, scene->world.objects[1].y, scene->world.objects[1].z);
    pushBlob(*scene, scene->world.cubes[1], scene->world.objects[2].x, scene->world.objects[2].y, scene->world.objects[2].z);
    pushBlob(*scene, scene->world.cubes[2], scene->world.objects[3].x, scene->world.objects[3].y, scene->world.objects[3].z);
    for (uint32_t i = 0; i < scene->world.lightCount; ++i) {
        scene->world.lights[i].radius = 8.0f;
    }
    scene->vertSrc = scene->cpuVerts;
    scene->indexSrc = scene->cpuIndex;
    scene->instSrc = scene->cpuInstances;
    scene->meshletSrc = scene->cpuMeshlets;
    scene->visibleCap = kSceneInstances;
    scene->visibleCpu.assign(scene->visibleCap, 0u);
    const float palette[4][3] = {{0.35f, 0.34f, 0.32f}, {0.80f, 0.25f, 0.20f}, {0.20f, 0.45f, 0.85f}, {0.85f, 0.75f, 0.30f}};
    for (uint32_t i = 0; i < 4; ++i) {
        scene->fallbackMat[i] = {};
        scene->fallbackMat[i].baseColor[0] = palette[i][0];
        scene->fallbackMat[i].baseColor[1] = palette[i][1];
        scene->fallbackMat[i].baseColor[2] = palette[i][2];
        scene->fallbackMat[i].baseColor[3] = 1.0f;
        scene->fallbackMat[i].roughness = 0.42f;
    }
    scene->materialSrc = scene->fallbackMat;
    scene->materialCount = 4;
    if (scene->instanceCount > 1) {
        linkProp(*scene, 1, "Cube");
        scene->actors.selected = 0;
    }
    armEditorRoom(*scene);
    return scene;
}

bool sceneFrameLoadBhop(SceneFrame* scene, const char* path, bool plant) {
    if (scene == nullptr || path == nullptr) {
        return false;
    }
    static_assert(sizeof(SceneVertex) == sizeof(GpuVertex));
    static_assert(sizeof(SceneInstance) == sizeof(GpuInstance));
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        spdlog::error("load map failed {}", path);
        return false;
    }
    SceneBlobView view{};
    if (!sceneBlobView(mapped.data, mapped.size, &view) || view.info->instanceCount == 0) {
        spdlog::error("load blob rejected {} bytes {}", path, mapped.size);
        fileUnmap(&mapped);
        return false;
    }
    fileUnmap(&scene->bhop);
    scene->bhop = mapped;
    strCopyCap(scene->bhopPath, sizeof(scene->bhopPath), path);
    scene->meshletGpu.assign(view.info->meshletCount, GpuMeshlet{});
    float minX = view.instances[0].sphere[0];
    float minY = view.instances[0].sphere[1];
    float minZ = view.instances[0].sphere[2];
    float maxX = minX;
    float maxY = minY;
    float maxZ = minZ;
    for (uint32_t i = 0; i < view.info->meshletCount; ++i) {
        const Meshlet& src = view.meshlets[i];
        GpuMeshlet& dst = scene->meshletGpu[i];
        dst = {};
        dst.vertexOffset = src.vertexOffset;
        dst.indexOffset = src.triangleOffset * 3u;
        dst.vertexCount = src.vertexCount;
        dst.triangleCount = src.triangleCount;
        dst.coneAxis[0] = src.coneAxis[0];
        dst.coneAxis[1] = src.coneAxis[1];
        dst.coneAxis[2] = src.coneAxis[2];
        dst.coneCutoff = src.coneCutoff;
    }
    writeGroupSpheres(reinterpret_cast<const GpuInstance*>(view.instances), scene->meshletGpu.data(), 0, view.info->instanceCount, view.info->meshletCount);
    for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
        const float x = view.instances[i].sphere[0];
        const float y = view.instances[i].sphere[1];
        const float z = view.instances[i].sphere[2];
        const float r = view.instances[i].sphere[3];
        if (x - r < minX) minX = x - r;
        if (y - r < minY) minY = y - r;
        if (z - r < minZ) minZ = z - r;
        if (x + r > maxX) maxX = x + r;
        if (y + r > maxY) maxY = y + r;
        if (z + r > maxZ) maxZ = z + r;
    }
    scene->visibleCpu.assign(view.info->instanceCount, 0u);
    scene->visibleCap = static_cast<uint32_t>(scene->visibleCpu.size());
    scene->vertCount = view.info->vertexCount;
    scene->indexCount = view.info->indexCount;
    scene->instanceCount = view.info->instanceCount;
    scene->vertSrc = reinterpret_cast<const GpuVertex*>(view.vertices);
    scene->indexSrc = view.indices;
    scene->instSrc = reinterpret_cast<const GpuInstance*>(view.instances);
    scene->meshletSrc = scene->meshletGpu.data();
    materialOwnFromView(*scene, view);
    scene->texNames = view.textures;
    scene->texNameCount = view.info->textureCount;
    scene->cones = view.meshlets;
    scene->coneCount = view.info->meshletCount;
    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    const float cz = (minZ + maxZ) * 0.5f;
    const float extent = std::max(maxX - minX, std::max(maxY - minY, maxZ - minZ));
    scene->boxMin[0] = minX;
    scene->boxMin[1] = minY;
    scene->boxMin[2] = minZ;
    scene->boxMax[0] = maxX;
    scene->boxMax[1] = maxY;
    scene->boxMax[2] = maxZ;
    scene->shadowExtent = std::max(20.0f, extent);
    scene->importedCount = std::min(view.info->lightCount, kLightCap);
    if (view.lights != nullptr && scene->importedCount > 0) {
        std::memcpy(scene->imported, view.lights, sizeof(LightGpuData) * scene->importedCount);
        int32_t slot = 0;
        for (uint32_t i = 0; i < scene->importedCount; ++i) {
            scene->imported[i].shadowMapIndex = -1;
            if (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot) {
                if (scene->imported[i].radius < 8.0f) {
                    scene->imported[i].radius = 22.0f;
                }
                if (scene->imported[i].intensity < 4.0f) {
                    scene->imported[i].intensity = 8.0f;
                }
            }
            if (scene->imported[i].type == kLightDirectional) {
                scene->world.sunDir[0] = scene->imported[i].direction[0];
                scene->world.sunDir[1] = scene->imported[i].direction[1];
                scene->world.sunDir[2] = scene->imported[i].direction[2];
                continue;
            }
            if (slot < 3 && (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot)) {
                scene->imported[i].shadowMapIndex = slot;
                scene->world.lights[slot] = scene->imported[i];
                slot += 1;
            }
        }
        if (slot > 0) {
            scene->world.lightCount = static_cast<uint32_t>(slot);
        }
    }
    const float ground = minY + 2.0f;
    if (scene->importedCount == 0 && scene->world.lightCount >= 3) {
        scene->world.lights[0].pos[0] = cx;
        scene->world.lights[0].pos[1] = ground + 3.0f;
        scene->world.lights[0].pos[2] = cz;
        scene->world.lights[1].pos[0] = cx + 12.0f;
        scene->world.lights[1].pos[1] = ground + 4.0f;
        scene->world.lights[1].pos[2] = cz + 6.0f;
        scene->world.lights[2].pos[0] = cx - 10.0f;
        scene->world.lights[2].pos[1] = ground + 5.0f;
        scene->world.lights[2].pos[2] = cz - 8.0f;
        for (uint32_t i = 0; i < 3; ++i) {
            scene->world.lights[i].radius = 24.0f;
        }
    }
    scene->farPlane = std::max(200.0f, extent * 4.0f);
    const float span = std::max(extent, 1.0e-3f);
    if (span < 4.0f) {
        scene->nearPlane = std::max(0.001f, std::min(0.05f, span * 0.02f));
        scene->fly.speed = std::max(0.15f, span * 0.8f);
    } else {
        scene->fly.speed = std::max(4.0f, span * 0.02f);
    }
    const float back = span < 4.0f ? std::max(span * 2.2f, 0.35f) : span * 0.05f;
    const float lift = span < 4.0f ? span * 0.35f : span * 0.15f;
    scene->fly.eye[0] = cx;
    scene->fly.eye[1] = cy + lift;
    scene->fly.eye[2] = maxZ + back;
    scene->fly.yaw = 0.0f;
    scene->fly.pitch = -0.15f;
    placeActors(*scene);
    scene->propCount = 0;
    scene->propPhase = 0;
    scene->propDirty = 0;
    scene->ready = false;
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    if (plant) {
        sceneFramePlantCubes(scene);
    } else {
        adoptLoadedModel(*scene, path, minX, minY, minZ, maxX, maxY, maxZ);
    }
    uint32_t hand = 0;
    for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
        if ((view.instances[i].pad[1] & 0x0fffffffu) >= kSceneLighterGroup) {
            hand += 1u;
        }
    }
    spdlog::info(
        "load {} verts {} idx {} inst {} mesh {} mat {} tex {} box {:.3f} {:.3f} {:.3f} .. {:.3f} {:.3f} {:.3f} eye {:.3f} {:.3f} {:.3f} yaw {:.2f} pitch {:.2f} near {:.4f} far {:.1f} speed {:.2f} hand {}",
        path, scene->vertCount, scene->indexCount, scene->instanceCount, view.info->meshletCount,
        scene->materialCount, scene->texNameCount,
        minX, minY, minZ, maxX, maxY, maxZ,
        scene->fly.eye[0], scene->fly.eye[1], scene->fly.eye[2],
        scene->fly.yaw, scene->fly.pitch, scene->nearPlane, scene->farPlane, scene->fly.speed, hand);
    return true;
}

void freeOwned(SceneFrame& s) {
    if (s.vertSrc == s.ownedVert.data()) {
        s.vertSrc = nullptr;
    }
    if (s.indexSrc == s.ownedIndex.data()) {
        s.indexSrc = nullptr;
    }
    if (s.instSrc == s.ownedInst.data()) {
        s.instSrc = nullptr;
    }
    if (s.meshletSrc == s.ownedMesh.data()) {
        s.meshletSrc = nullptr;
    }
    if (s.materialSrc == s.ownedMat.data()) {
        s.materialSrc = nullptr;
    }
    if (s.texNames == s.ownedTex.data()) {
        s.texNames = nullptr;
    }
    s.ownedVert.clear();
    s.ownedIndex.clear();
    s.ownedInst.clear();
    s.ownedMesh.clear();
    s.ownedMat.clear();
    s.ownedTex.clear();
}

void sceneFrameShift(SceneFrame* scene, float dx, float dy, float dz) {
    if (scene == nullptr || scene->instanceCount == 0 || scene->instSrc == nullptr) {
        return;
    }
    if (scene->instSrc != scene->ownedInst.data()) {
        scene->ownedInst.assign(scene->instSrc, scene->instSrc + scene->instanceCount);
        scene->instSrc = scene->ownedInst.data();
    }
    for (uint32_t i = 0; i < scene->instanceCount; ++i) {
        GpuInstance& inst = scene->ownedInst[i];
        inst.world[3] += dx;
        inst.world[7] += dy;
        inst.world[11] += dz;
        inst.sphere[0] += dx;
        inst.sphere[1] += dy;
        inst.sphere[2] += dz;
    }
    scene->boxMin[0] += dx;
    scene->boxMin[1] += dy;
    scene->boxMin[2] += dz;
    scene->boxMax[0] += dx;
    scene->boxMax[1] += dy;
    scene->boxMax[2] += dz;
}

void sceneFrameBlank(SceneFrame* scene) {
    if (scene == nullptr) {
        return;
    }
    fileUnmap(&scene->bhop);
    scene->bhopPath[0] = '\0';
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    freeOwned(*scene);
    scene->meshletGpu.clear();
    scene->vertCount = 0;
    scene->indexCount = 0;
    scene->instanceCount = 0;
    scene->materialCount = 0;
    scene->texNameCount = 0;
    scene->propCount = 0;
    scene->vertSrc = scene->cpuVerts;
    scene->indexSrc = scene->cpuIndex;
    scene->instSrc = scene->cpuInstances;
    scene->meshletSrc = scene->cpuMeshlets;
    scene->materialSrc = nullptr;
    scene->texNames = nullptr;
    scene->cones = nullptr;
    scene->coneCount = 0;
    placeActors(*scene);
    armEditorRoom(*scene);
    scene->ready = false;
}

bool sceneFrameAppendBhop(SceneFrame* scene, const char* path) {
    if (scene == nullptr || path == nullptr) {
        return false;
    }
    spdlog::info("append {} base verts {} inst {}", path, scene->vertCount, scene->instanceCount);
    if (scene->vertSrc == nullptr || scene->vertCount == 0 || scene->instanceCount == 0) {
        spdlog::info("append empty scene, load {}", path);
        return sceneFrameLoadBhop(scene, path, false);
    }
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        spdlog::error("append map failed {}", path);
        return false;
    }
    SceneBlobView view{};
    if (!sceneBlobView(mapped.data, mapped.size, &view) || view.info->instanceCount == 0) {
        spdlog::error("append blob rejected {} bytes {}", path, mapped.size);
        fileUnmap(&mapped);
        return false;
    }
    const uint32_t vertBase = scene->vertCount;
    const uint32_t indexBase = scene->indexCount;
    const uint32_t instBase = scene->instanceCount;
    const uint32_t matBase = scene->materialCount;
    const uint32_t texBase = scene->texNameCount == 0 ? 1u : scene->texNameCount;
    const uint32_t addTex = view.info->textureCount > 0 ? view.info->textureCount - 1u : 0u;
    const uint32_t vertCount = vertBase + view.info->vertexCount + 4u;
    const uint32_t indexCount = indexBase + view.info->indexCount + 6u;
    const uint32_t instCount = instBase + view.info->instanceCount + 1u;
    const uint32_t matCount = matBase + view.info->materialCount + 1u;
    const uint32_t texCount = texBase + addTex;
    std::vector<GpuVertex> verts(vertCount);
    std::vector<uint32_t> indices(indexCount);
    std::vector<GpuInstance> instances(instCount);
    std::vector<GpuMeshlet> meshlets(instCount);
    std::vector<MaterialGpuData> materials(matCount == 0 ? 1u : matCount);
    std::vector<SceneTexName> names(texCount == 0 ? 1u : texCount);
    if (vertBase > 0) {
        std::memcpy(verts.data(), scene->vertSrc, sizeof(GpuVertex) * vertBase);
    }
    if (indexBase > 0) {
        std::memcpy(indices.data(), scene->indexSrc, sizeof(uint32_t) * indexBase);
    }
    if (instBase > 0) {
        std::memcpy(instances.data(), scene->instSrc, sizeof(GpuInstance) * instBase);
        std::memcpy(meshlets.data(), scene->meshletSrc, sizeof(GpuMeshlet) * instBase);
    }
    if (scene->materialSrc != nullptr && matBase > 0) {
        std::memcpy(materials.data(), scene->materialSrc, sizeof(MaterialGpuData) * matBase);
    }
    if (scene->texNames != nullptr && scene->texNameCount > 0) {
        std::memcpy(names.data(), scene->texNames, sizeof(SceneTexName) * scene->texNameCount);
    }
    std::memcpy(verts.data() + vertBase, view.vertices, sizeof(GpuVertex) * view.info->vertexCount);
    std::memcpy(indices.data() + indexBase, view.indices, sizeof(uint32_t) * view.info->indexCount);
    float minX = 1.0e9f;
    float minY = 1.0e9f;
    float minZ = 1.0e9f;
    float maxX = -1.0e9f;
    float maxY = -1.0e9f;
    float maxZ = -1.0e9f;
    for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
        GpuInstance& dst = instances[instBase + i];
        const SceneInstance& src = view.instances[i];
        std::copy_n(src.world, std::size(src.world), dst.world);
        std::copy_n(src.sphere, std::size(src.sphere), dst.sphere);
        dst.materialId = src.materialId;
        dst.meshId = src.meshId;
        std::copy_n(src.pad, std::size(src.pad), dst.pad);
        dst.materialId += matBase;
        dst.meshId += instBase;
        const float x = dst.sphere[0];
        const float y = dst.sphere[1];
        const float z = dst.sphere[2];
        const float r = dst.sphere[3];
        minX = std::min(minX, x - r);
        minY = std::min(minY, y - r);
        minZ = std::min(minZ, z - r);
        maxX = std::max(maxX, x + r);
        maxY = std::max(maxY, y + r);
        maxZ = std::max(maxZ, z + r);
    }
    for (uint32_t i = 0; i < view.info->meshletCount && instBase + i < instCount; ++i) {
        const Meshlet& src = view.meshlets[i];
        GpuMeshlet& dst = meshlets[instBase + i];
        dst = {};
        dst.vertexOffset = src.vertexOffset + vertBase;
        dst.indexOffset = src.triangleOffset * 3u + indexBase;
        dst.vertexCount = src.vertexCount;
        dst.triangleCount = src.triangleCount;
        dst.coneAxis[0] = src.coneAxis[0];
        dst.coneAxis[1] = src.coneAxis[1];
        dst.coneAxis[2] = src.coneAxis[2];
        dst.coneCutoff = src.coneCutoff;
    }
    writeGroupSpheres(instances.data(), meshlets.data(), instBase, view.info->instanceCount, static_cast<uint32_t>(meshlets.size()));
    for (uint32_t i = 0; i < addTex; ++i) {
        names[texBase + i] = view.textures[i + 1u];
    }
    for (uint32_t i = 0; i < view.info->materialCount; ++i) {
        MaterialGpuData& dst = materials[matBase + i];
        const uint8_t* raw = view.materialRaw != nullptr ? view.materialRaw + static_cast<size_t>(i) * view.materialStride : nullptr;
        materialAdoptRecord(dst, raw, view.materialStride);
        dst.albedoTexIndex = remapTex(dst.albedoTexIndex, texBase);
        dst.normalTexIndex = remapTex(dst.normalTexIndex, texBase);
        dst.ormTexIndex = remapTex(dst.ormTexIndex, texBase);
        dst.emissiveTexIndex = remapTex(dst.emissiveTexIndex, texBase);
        dst.pad[0] = remapTex(dst.pad[0], texBase);
        dst.pad[2] = remapTex(dst.pad[2], texBase);
        dst.pad[3] = remapTex(dst.pad[3], texBase);
        dst.aoTex = remapTex(dst.aoTex, texBase);
    }
    const uint32_t mirrorVert = vertBase + view.info->vertexCount;
    const uint32_t mirrorIndex = indexBase + view.info->indexCount;
    const uint32_t mirrorInst = instBase + view.info->instanceCount;
    const uint32_t mirrorMat = matBase + view.info->materialCount;
    const float mirrorX = (minX + maxX) * 0.5f;
    const float mirrorY = minY + 1.6f;
    const float mirrorZ = maxZ - 4.0f;
    const float mirrorW = 1.2f;
    const float mirrorH = 1.8f;
    const float mirrorPos[4][4] = {
        {mirrorX - mirrorW, mirrorY - mirrorH, 0.0f, 1.0f},
        {mirrorX + mirrorW, mirrorY - mirrorH, 1.0f, 1.0f},
        {mirrorX + mirrorW, mirrorY + mirrorH, 1.0f, 0.0f},
        {mirrorX - mirrorW, mirrorY + mirrorH, 0.0f, 0.0f},
    };
    for (uint32_t i = 0; i < 4u; ++i) {
        GpuVertex& vert = verts[mirrorVert + i];
        vert = {};
        vert.px = mirrorPos[i][0];
        vert.py = mirrorPos[i][1];
        vert.pz = mirrorZ;
        vert.pad0 = mirrorPos[i][2];
        vert.nz = 1.0f;
        vert.pad1 = mirrorPos[i][3];
    }
    const uint32_t mirrorTri[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    for (uint32_t i = 0; i < 6u; ++i) {
        indices[mirrorIndex + i] = mirrorTri[i];
    }
    GpuMeshlet& mirrorMesh = meshlets[mirrorInst];
    mirrorMesh = {};
    mirrorMesh.vertexOffset = mirrorVert;
    mirrorMesh.indexOffset = mirrorIndex;
    mirrorMesh.vertexCount = 4;
    mirrorMesh.triangleCount = 2;
    mirrorMesh.coneAxis[2] = 1.0f;
    mirrorMesh.coneCutoff = -1.0f;
    instanceSet(instances[mirrorInst], 0.0f, 0.0f, 0.0f, 3.0f, mirrorMat, mirrorInst);
    instances[mirrorInst].sphere[0] = mirrorX;
    instances[mirrorInst].sphere[1] = mirrorY;
    instances[mirrorInst].sphere[2] = mirrorZ;
    instances[mirrorInst].sphere[3] = 3.0f;
    MaterialGpuData& mirror = materials[mirrorMat];
    mirror = {};
    mirror.baseColor[0] = mirror.baseColor[1] = mirror.baseColor[2] = mirror.baseColor[3] = 1.0f;
    mirror.roughness = 0.02f;
    mirror.metallic = 1.0f;
    freeOwned(*scene);
    scene->ownedVert = std::move(verts);
    scene->ownedIndex = std::move(indices);
    scene->ownedInst = std::move(instances);
    scene->ownedMesh = std::move(meshlets);
    scene->ownedMat = std::move(materials);
    scene->ownedTex = std::move(names);
    scene->vertSrc = scene->ownedVert.data();
    scene->indexSrc = scene->ownedIndex.data();
    scene->instSrc = scene->ownedInst.data();
    scene->propCount = 0;
    scene->propPhase = 0;
    scene->propDirty = 0;
    scene->meshletSrc = scene->ownedMesh.data();
    scene->materialSrc = scene->ownedMat.data();
    scene->texNames = scene->ownedTex.data();
    scene->vertCount = vertCount;
    scene->indexCount = indexCount;
    scene->instanceCount = instCount;
    scene->materialCount = matCount;
    scene->texNameCount = texCount;
    scene->visibleCpu.assign(instCount, 0u);
    scene->visibleCap = static_cast<uint32_t>(scene->visibleCpu.size());
    scene->meshletGpu.clear();
    scene->cones = nullptr;
    scene->coneCount = 0;
    if (view.lights != nullptr && view.info->lightCount > 0) {
        LightGpuData merged[kLightCap]{};
        uint32_t n = 0;
        const uint32_t add = std::min(view.info->lightCount, kLightCap);
        std::memcpy(merged, view.lights, sizeof(LightGpuData) * add);
        n = add;
        for (uint32_t i = 0; i < scene->importedCount && n < kLightCap; ++i) {
            merged[n++] = scene->imported[i];
        }
        std::memcpy(scene->imported, merged, sizeof(LightGpuData) * n);
        scene->importedCount = n;
        int32_t slot = 0;
        scene->world.lightCount = 0;
        for (uint32_t i = 0; i < n; ++i) {
            scene->imported[i].shadowMapIndex = -1;
            if (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot) {
                if (scene->imported[i].radius < 8.0f) {
                    scene->imported[i].radius = 22.0f;
                }
                if (scene->imported[i].intensity < 4.0f) {
                    scene->imported[i].intensity = 8.0f;
                }
            }
            if (scene->imported[i].type == kLightDirectional) {
                scene->world.sunDir[0] = scene->imported[i].direction[0];
                scene->world.sunDir[1] = scene->imported[i].direction[1];
                scene->world.sunDir[2] = scene->imported[i].direction[2];
                continue;
            }
            if (slot < 3 && (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot)) {
                scene->imported[i].shadowMapIndex = slot;
                scene->world.lights[slot] = scene->imported[i];
                slot += 1;
            }
        }
        scene->world.lightCount = static_cast<uint32_t>(slot);
    }
    const float extent = std::max(maxX - minX, std::max(maxY - minY, maxZ - minZ));
    scene->shadowExtent = std::max(20.0f, extent);
    scene->farPlane = std::max(scene->farPlane, std::max(400.0f, extent * 4.0f));
    scene->farPlane = std::max(scene->farPlane, 8000.0f);
    scene->fly.eye[0] = (minX + maxX) * 0.5f;
    scene->fly.eye[1] = std::max(2.0f, minY + std::max(2.0f, extent * 0.15f));
    scene->fly.eye[2] = maxZ + std::max(4.0f, extent * 0.05f);
    scene->fly.yaw = 0.0f;
    scene->fly.pitch = -0.1f;
    scene->fly.speed = 40.0f;
    placeActors(*scene);
    scene->boxMin[0] = std::min(scene->boxMin[0], minX);
    scene->boxMin[1] = std::min(scene->boxMin[1], minY);
    scene->boxMin[2] = std::min(scene->boxMin[2], minZ);
    scene->boxMax[0] = std::max(scene->boxMax[0], maxX);
    scene->boxMax[1] = std::max(scene->boxMax[1], maxY);
    scene->boxMax[2] = std::max(scene->boxMax[2], maxZ);
    spdlog::info(
        "append merged {} added inst {} verts {} now inst {} verts {} eye {:.3f} {:.3f} {:.3f}",
        path, view.info->instanceCount, view.info->vertexCount, scene->instanceCount, scene->vertCount,
        scene->fly.eye[0], scene->fly.eye[1], scene->fly.eye[2]);
    fileUnmap(&mapped);
    armEditorRoom(*scene);
    const char* leaf = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            leaf = p + 1;
        }
    }
    const uint32_t added = view.info->instanceCount < 8u ? view.info->instanceCount : 8u;
    for (uint32_t i = 0; i < added && scene->propCount < kPropCap; ++i) {
        char name[24]{};
        if (scene->instSrc != nullptr && instBase + i < scene->instanceCount) {
            modelLabel(*scene, scene->instSrc[instBase + i].materialId, name, sizeof(name));
        }
        if (name[0] == '\0') {
            std::snprintf(name, sizeof(name), "%s", leaf);
        }
        linkProp(*scene, instBase + i, name);
    }
    scene->ready = false;
    return true;
}

bool syncPropInstance(SceneFrame* scene) {
    syncMeshActors(*scene);
    GpuInstance* table = nullptr;
    if (scene->instSrc == scene->cpuInstances) {
        table = scene->cpuInstances;
    } else if (!scene->ownedInst.empty() && scene->instSrc == scene->ownedInst.data()) {
        table = scene->ownedInst.data();
    }
    if (scene->propCount == 0 || table == nullptr) {
        return false;
    }
    bool moved = false;
    for (uint8_t n = 0; n < scene->propCount; ++n) {
        SceneFrameImpl::PropLink& link = scene->props[n];
        if (link.actor == 0xff || link.instance >= scene->instanceCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene->actors, link.actor);
        const Xform& prev = link.pose;
        const float dp = (pose.pos[0] - prev.pos[0]) * (pose.pos[0] - prev.pos[0])
            + (pose.pos[1] - prev.pos[1]) * (pose.pos[1] - prev.pos[1])
            + (pose.pos[2] - prev.pos[2]) * (pose.pos[2] - prev.pos[2]);
        const float de = (pose.euler[0] - prev.euler[0]) * (pose.euler[0] - prev.euler[0])
            + (pose.euler[1] - prev.euler[1]) * (pose.euler[1] - prev.euler[1])
            + (pose.euler[2] - prev.euler[2]) * (pose.euler[2] - prev.euler[2]);
        const float ds = (pose.scale[0] - prev.scale[0]) * (pose.scale[0] - prev.scale[0])
            + (pose.scale[1] - prev.scale[1]) * (pose.scale[1] - prev.scale[1])
            + (pose.scale[2] - prev.scale[2]) * (pose.scale[2] - prev.scale[2]);
        if (dp < 1.0e-8f && de < 1.0e-6f && ds < 1.0e-8f) {
            continue;
        }
        float axisX[3]{};
        float axisY[3]{};
        float axisZ[3]{};
        actorBasis(pose.euler, axisX, axisY, axisZ);
        GpuInstance& cpu = table[link.instance];
        cpu.world[0] = axisX[0] * pose.scale[0];
        cpu.world[4] = axisX[1] * pose.scale[0];
        cpu.world[8] = axisX[2] * pose.scale[0];
        cpu.world[1] = axisY[0] * pose.scale[1];
        cpu.world[5] = axisY[1] * pose.scale[1];
        cpu.world[9] = axisY[2] * pose.scale[1];
        cpu.world[2] = axisZ[0] * pose.scale[2];
        cpu.world[6] = axisZ[1] * pose.scale[2];
        cpu.world[10] = axisZ[2] * pose.scale[2];
        cpu.world[3] = pose.pos[0];
        cpu.world[7] = pose.pos[1];
        cpu.world[11] = pose.pos[2];
        cpu.sphere[0] = pose.pos[0];
        cpu.sphere[1] = pose.pos[1];
        cpu.sphere[2] = pose.pos[2];
        const float smax = std::max(pose.scale[0], std::max(pose.scale[1], pose.scale[2]));
        cpu.sphere[3] = link.radius * std::max(smax, 0.001f);
        if (scene->instances.mapped != nullptr) {
            auto* gpu = static_cast<GpuInstance*>(scene->instances.mapped);
            gpu[link.instance] = cpu;
        }
        link.pose = pose;
        moved = true;
    }
    return moved;
}


} // namespace burnhope
