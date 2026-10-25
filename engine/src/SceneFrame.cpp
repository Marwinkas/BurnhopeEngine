#include "audio/Audio.hpp"
#include "render/PassBufferSync.hpp"
#include "render/Physics.hpp"
#include "render/Weather.hpp"
#include "render/SceneFrameImpl.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace burnhope {

const char* sceneFrameGpuText(const SceneFrame* scene) {
    if (scene == nullptr) {
        return "";
    }
    return scene->gpuText;
}

void sceneFrameDestroy(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    sceneFrameReleaseGpu(scene, device);
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    audioDestroy(scene->audio);
    scene->audio = nullptr;
    fileUnmap(&scene->bhop);
    freeOwned(*scene);
    scene->meshletGpu.clear();
    scene->visibleCpu.clear();
    delete scene;
}

bool sceneFrameTick(SceneFrame* scene, const InputFrame& input, float dt) {
    if (scene == nullptr) {
        return false;
    }
    float gpu = 0.0f;
    const uint32_t zones = scene->gpuTime.count < 48u ? scene->gpuTime.count : 48u;
    for (uint32_t i = 0; i < zones; ++i) {
        gpu += scene->gpuTime.ms[i];
    }
    scene->gpuMs = gpu;
    if (scene->meshletSrc != nullptr && scene->instSrc != nullptr) {
        uint32_t tris = 0;
        for (uint32_t i = 0; i < scene->instanceCount; ++i) {
            const uint32_t mesh = scene->instSrc[i].meshId;
            if (mesh < scene->instanceCount) {
                tris += scene->meshletSrc[mesh].triangleCount;
            }
        }
        scene->sceneTris = tris;
    }
    sceneActorPointer(
        scene->actors,
        input.pointer.x,
        input.pointer.y,
        (input.pointer.down & kPointerLeft) != 0 ? 1 : 0,
        (input.pointer.pressed & kPointerLeft) != 0 ? 1 : 0,
        (input.pointer.released & kPointerLeft) != 0 ? 1 : 0);
    if (scene->physics != nullptr && scene->sim != 0) {
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
        const uint8_t held = scene->actors.drag <= 6 ? scene->actors.selected : kSceneActorNone;
        const float stepDt = scene->sim == 2 ? 0.0f : dt;
        const auto physicsBegan = std::chrono::steady_clock::now();
        physicsStep(scene->physics, stepDt, held, poses, count);
        scene->physicsMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - physicsBegan).count();
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            if (actor == held || actor >= scene->actors.count) {
                continue;
            }
            Xform pose = sceneActorXform(scene->actors, actor);
            pose.pos[0] = poses[i].pos[0];
            pose.pos[1] = poses[i].pos[1];
            pose.pos[2] = poses[i].pos[2];
            pose.euler[0] = poses[i].euler[0];
            pose.euler[1] = poses[i].euler[1];
            pose.euler[2] = poses[i].euler[2];
            pose.scale[0] = poses[i].scale[0];
            pose.scale[1] = poses[i].scale[1];
            pose.scale[2] = poses[i].scale[2];
            sceneActorWriteXform(scene->actors, actor, pose);
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            const float y = poses[i].pos[1];
            const float vy = dt > 1.0e-4f ? (y - scene->impactY[i]) / dt : 0.0f;
            if (actor != held && scene->impactVy[i] < -6.0f && vy > -0.8f) {
                particleBurst(scene->particles, poses[i].pos[0], poses[i].pos[1], poses[i].pos[2]);
            }
            scene->impactY[i] = y;
            scene->impactVy[i] = vy;
        }
    }
    const bool plan = scene->camFacing != 0 || scene->camOrtho != 0;
    InputFrame moved = input;
    if (plan && input.wheel != 0.0f) {
        moved.wheel = 0.0f;
        if (scene->camOrtho != 0) {
            scene->camZoom *= std::exp(-input.wheel * 0.08f);
            if (scene->camZoom < 0.05f) {
                scene->camZoom = 0.05f;
            }
            if (scene->camZoom > 32.0f) {
                scene->camZoom = 32.0f;
            }
        } else {
            float fx = 0.0f;
            float fy = 0.0f;
            float fz = 0.0f;
            flyCamForward(scene->fly, fx, fy, fz);
            const float dolly = -input.wheel * scene->fly.speed * 0.35f;
            scene->fly.eye[0] += fx * dolly;
            scene->fly.eye[1] += fy * dolly;
            scene->fly.eye[2] += fz * dolly;
        }
    }
    if (plan) {
        flyCamPan(scene->fly, moved, dt, scene->actors.right, scene->actors.up);
    } else {
        flyCamUpdate(scene->fly, moved, dt);
    }
    for (uint8_t i = 0; i < scene->actors.count; ++i) {
        flecs::entity ent = scene->actors.ent[i];
        if (!ent.is_alive() || !ent.has<LightLink>()) {
            continue;
        }
        const LightLink link = ent.get<LightLink>();
        if (link.index >= scene->importedCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene->actors, i);
        LightGpuData& lamp = scene->imported[link.index];
        lamp.pos[0] = pose.pos[0];
        lamp.pos[1] = pose.pos[1];
        lamp.pos[2] = pose.pos[2];
        if (lamp.type == kLightDirectional) {
            float fx = 0.0f;
            float fy = 0.0f;
            float fz = 0.0f;
            float bx[3]{};
            float by[3]{};
            float bz[3]{};
            actorBasis(pose.euler, bx, by, bz);
            fx = -by[0];
            fy = -by[1];
            fz = -by[2];
            lamp.direction[0] = fx;
            lamp.direction[1] = fy;
            lamp.direction[2] = fz;
            scene->world.sunDir[0] = fx;
            scene->world.sunDir[1] = fy;
            scene->world.sunDir[2] = fz;
        }
    }
    if (dt > 1.0e-4f) {
        scene->skyTime += dt;
    }
    scene->simDt = dt > 0.05f ? 0.05f : (dt < 0.0f ? 0.0f : dt);
    if (Weather* climate = scene->weatherEnt.try_get_mut<Weather>()) {
        weatherIntegrate(*climate, scene->simDt);
    }
    particleTick(scene->particles, scene->simDt);
    if (scene->particles.seed > scene->camBursts) {
        float kick = static_cast<float>(scene->particles.seed - scene->camBursts) * 0.08f;
        if (kick > 0.35f) {
            kick = 0.35f;
        }
        scene->camShock += kick;
        if (scene->camShock > 0.55f) {
            scene->camShock = 0.55f;
        }
        scene->camBursts = scene->particles.seed;
    }
    scene->camShockVel += -scene->camShock * 42.0f * scene->simDt;
    scene->camShockVel *= std::exp(-6.5f * scene->simDt);
    scene->camShock += scene->camShockVel * scene->simDt;
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = 1.0f;
    flyCamForward(scene->fly, fx, fy, fz);
    if (inputKeyEdge(input, 10)) {
        particleBurst(
            scene->particles,
            scene->fly.eye[0] + fx * 6.0f,
            scene->fly.eye[1] + fy * 6.0f,
            scene->fly.eye[2] + fz * 6.0f);
    }
    if (scene->audio != nullptr) {
        const float roof = physicsRoof(scene->physics, scene->fly.eye[0], scene->fly.eye[1], scene->fly.eye[2]) ? 1.0f : 0.0f;
        AudioListener ear{};
        ear.origin[0] = scene->fly.eye[0];
        ear.origin[1] = scene->fly.eye[1];
        ear.origin[2] = scene->fly.eye[2];
        ear.ahead[0] = fx;
        ear.ahead[1] = fy;
        ear.ahead[2] = fz;
        const float rx = -fz;
        const float ry = 0.0f;
        const float rz = fx;
        ear.up[0] = ry * fz - rz * fy;
        ear.up[1] = rz * fx - rx * fz;
        ear.up[2] = rx * fy - ry * fx;
        const float ul = std::sqrt(ear.up[0] * ear.up[0] + ear.up[1] * ear.up[1] + ear.up[2] * ear.up[2]);
        if (ul < 1.0e-4f) {
            ear.up[0] = 0.0f;
            ear.up[1] = 1.0f;
            ear.up[2] = 0.0f;
        } else {
            ear.up[0] /= ul;
            ear.up[1] /= ul;
            ear.up[2] /= ul;
        }
        audioSetListener(scene->audio, ear);
        audioSetRain(scene->audio, 1.0f - roof, scene->rainAmount);
        audioTick(scene->audio, scene->simDt);
    }
    if (scene->particles.intro == 0 && scene->skyTime > 0.6f) {
        scene->particles.intro = 1;
        particleBurst(
            scene->particles,
            scene->fly.eye[0] + fx * 8.0f,
            scene->fly.eye[1] + fy * 8.0f - 1.0f,
            scene->fly.eye[2] + fz * 8.0f);
    }
    return (input.pointer.down & kPointerRight) != 0;
}

void sceneFramePresented(SceneFrame* scene, float dt) {
    if (scene == nullptr || dt <= 1.0e-4f) {
        return;
    }
    scene->frameMs[scene->frameCursor % 128u] = dt * 1000.0f;
    scene->frameCursor += 1;
    const uint32_t filled = scene->frameCursor < 128u ? scene->frameCursor : 128u;
    const uint32_t recent = filled < 32u ? filled : 32u;
    double sum = 0.0;
    for (uint32_t i = 0; i < recent; ++i) {
        sum += static_cast<double>(scene->frameMs[(scene->frameCursor - 1u - i) % 128u]);
    }
    scene->fps = sum > 0.0 ? static_cast<float>((1000.0 * static_cast<double>(recent)) / sum) : 0.0f;
}

uint8_t sceneFrameSim(const SceneFrame* scene) {
    return scene != nullptr ? scene->sim : 0;
}

float sceneFrameFps(const SceneFrame* scene) {
    return scene != nullptr ? scene->fps : 0.0f;
}

float sceneFramePhysicsMs(const SceneFrame* scene) {
    return scene != nullptr ? scene->physicsMs : 0.0f;
}

float sceneFrameGpuMs(const SceneFrame* scene) {
    return scene != nullptr ? scene->gpuMs : 0.0f;
}

uint32_t sceneFrameTriangles(const SceneFrame* scene) {
    return scene != nullptr ? scene->sceneTris : 0;
}

uint32_t sceneFrameDraws(const SceneFrame* scene) {
    return scene != nullptr ? scene->visibleCount : 0;
}

const char* sceneFrameTitle(const SceneFrame* scene) {
    return scene != nullptr ? scene->title : "";
}

void sceneFrameSetDecals(SceneFrame* scene, const DecalGpu* decals, uint32_t count) {
    if (scene == nullptr) {
        return;
    }
    scene->decalCount = 0;
    if (decals == nullptr || count == 0 || scene->decalBuf.mapped == nullptr) {
        return;
    }
    if (count > kDecalCap) {
        count = kDecalCap;
    }
    std::memcpy(scene->decals, decals, sizeof(DecalGpu) * count);
    std::memcpy(scene->decalBuf.mapped, scene->decals, sizeof(DecalGpu) * count);
    scene->decalCount = count;
}

void sceneFrameSetSectors(SceneFrame* scene, const Sector* sectors, uint32_t sectorCount, const Portal* portals, uint32_t portalCount) {
    if (scene == nullptr) {
        return;
    }
    scene->sectorCount = 0;
    scene->portalCount = 0;
    if (sectors != nullptr && sectorCount > 0) {
        if (sectorCount > 32u) {
            sectorCount = 32u;
        }
        std::memcpy(scene->sectors, sectors, sizeof(Sector) * sectorCount);
        scene->sectorCount = sectorCount;
    }
    if (portals != nullptr && portalCount > 0) {
        if (portalCount > 64u) {
            portalCount = 64u;
        }
        std::memcpy(scene->portals, portals, sizeof(Portal) * portalCount);
        scene->portalCount = portalCount;
    }
}

void sceneFrameDebug(SceneFrame* scene, bool hiz, bool cone, bool lod, bool frustum, bool subpixel, bool shadow, bool backface, bool screen, bool behind, uint32_t mode) {
    if (scene == nullptr) {
        return;
    }
    if (mode > 19u) {
        mode = 0;
    }
    if (scene->showHiz == hiz && scene->showCone == cone
        && scene->showLod == lod && scene->showFrustum == frustum && scene->showSubpixel == subpixel
        && scene->showShadow == shadow && scene->showBackface == backface
        && scene->showScreen == screen && scene->showBehind == behind
        && scene->debugMode == mode) {
        return;
    }
    scene->showHiz = hiz;
    scene->showCone = cone;
    scene->showLod = lod;
    scene->showFrustum = frustum;
    scene->showSubpixel = subpixel;
    scene->showShadow = shadow;
    scene->showBackface = backface;
    scene->showScreen = screen;
    scene->showBehind = behind;
    scene->debugMode = mode;
    spdlog::info("debug profile {} hiz {} cone {} lod {} frust {} small {} shadow {} mode {}",
        scene->lightProfile, hiz ? 1 : 0, cone ? 1 : 0, lod ? 1 : 0, frustum ? 1 : 0, subpixel ? 1 : 0, shadow ? 1 : 0, mode);
}

void sceneFrameStats(const SceneFrame* scene, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (scene == nullptr) {
        return;
    }
    const uint32_t n = scene->frameCursor < 128u ? scene->frameCursor : 128u;
    float copy[128];
    float worst = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        copy[i] = scene->frameMs[i];
        if (copy[i] > worst) {
            worst = copy[i];
        }
    }
    for (uint32_t i = 1; i < n; ++i) {
        const float key = copy[i];
        uint32_t j = i;
        while (j > 0 && copy[j - 1] < key) {
            copy[j] = copy[j - 1];
            j -= 1;
        }
        copy[j] = key;
    }
    const float low = worst > 0.0f ? 1000.0f / worst : scene->fps;
    uint32_t slowN = n / 100u;
    if (slowN < 1u && n > 0u) {
        slowN = 1u;
    }
    float slowSum = 0.0f;
    for (uint32_t i = 0; i < slowN; ++i) {
        slowSum += copy[i];
    }
    const float one = slowSum > 0.0f ? (1000.0f * static_cast<float>(slowN)) / slowSum : scene->fps;
    std::snprintf(out, cap, "fps %.0f   low %.0f   1%% %.0f   mesh %u/%u", scene->fps, low, one, scene->visibleCount, scene->instanceCount);
}

void sceneFrameKick(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    RadiancePass& rad = scene->radiance;
    const bool loaded = scene->kickHeaps != nullptr && scene->vertSrc != nullptr;
    if (loaded && !rad.geometry && rad.buildTask.stage == 0) {
        uint32_t meshCount = scene->instanceCount;
        if (scene->meshletSrc == scene->meshletGpu.data()) {
            meshCount = static_cast<uint32_t>(scene->meshletGpu.size());
        } else if (scene->meshletSrc == scene->ownedMesh.data()) {
            meshCount = static_cast<uint32_t>(scene->ownedMesh.size());
        }
        radianceArm(rad, device, *scene->kickHeaps, scene->vertSrc, scene->indexSrc, scene->instSrc, scene->meshletSrc,
            scene->instanceCount, scene->vertCount, scene->indexCount, meshCount);
    }
    radianceKick(rad, device);
}

void sceneFrameRecord(
    SceneFrame* scene,
    VkCommandBuffer cmd,
    Device& device,
    DescriptorHeaps& heaps,
    const Swapchain& swap,
    const FrameContext& frame) {
    if (scene == nullptr || swap.extent.width == 0 || swap.extent.height == 0) {
        return;
    }
    followHandLamp(*scene);
    if (scene->handOn && scene->instances.mapped != nullptr && scene->instanceCount > 0) {
        gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
    }
    (void)heaps;
    CpuScope recordCpu("sceneFrameRecord", &scene->cpuRecordMs);
    const bool fsrLive = (scene->passMask & kPassFsr) != 0u && scene->fsr.prepare.handle != VK_NULL_HANDLE;
    const VkExtent2D renderExtent = fsrLive ? fsrRenderExtent(scene->fsr.quality, swap.extent) : swap.extent;
    sceneResizeTargets(scene, device, swap, frame, renderExtent);
    const bool propMoved = syncPropInstance(scene);
    syncPropMaterial(*scene);
    if (propMoved && scene->instances.mapped != nullptr && scene->instanceCount > 0) {
        gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
    }
    if ((scene->editorDirty & 2u) != 0u) {
        if (scene->verts.mapped != nullptr && scene->vertCount > 0) {
            gpuBufferFlush(scene->verts, device, 0, sizeof(GpuVertex) * scene->vertCount);
        }
        if (scene->indices.mapped != nullptr && scene->indexCount > 0) {
            gpuBufferFlush(scene->indices, device, 0, sizeof(uint32_t) * scene->indexCount);
        }
        if (scene->instances.mapped != nullptr && scene->instanceCount > 0) {
            gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
        }
        if (scene->meshlets.mapped != nullptr && scene->instanceCount > 0) {
            gpuBufferFlush(scene->meshlets, device, 0, sizeof(GpuMeshlet) * scene->instanceCount);
        }
    }
    if ((scene->editorDirty & 1u) != 0u && scene->materialsBuf.mapped != nullptr && scene->materialCount > 0) {
        gpuBufferFlush(scene->materialsBuf, device, 0, sizeof(MaterialGpuData) * scene->materialCount);
    }
    scene->editorDirty = 0;
    scene->radiance.pause = 0;
    if (!scene->ready && !sceneCreateGpu(scene, device, swap)) {
        return;
    }
    if (scene->ready) {
        takeQueuedTexture(*scene, device);
    }
    SceneUniformSet u{};
    sceneFillUniforms(scene, device, swap, frame, fsrLive, u);
    FrameView& gpu = u.gpu;
    FrameSun& lit = u.lit;
    FramePost& knob = u.knob;
    const CameraPose& pose = u.pose;
    const uint32_t flight = u.flight;
    const uint32_t strideU = u.strideU;
    const bool taaLive = u.taaLive;
    const LightGpuData* lightSrc = u.lightSrc;
    const uint32_t lightCount = u.lightCount;
    const float (&unjitVp)[16] = u.unjitVp;
    const float (&jitInv)[16] = u.jitInv;
    const uint32_t (&sunKeep)[3] = u.sunKeep;
    const float (&splits)[3] = u.splits;
    scene->kickHeaps = &scene->gpuHeap;
    scene->kickFlight = flight;
    if (scene->shadowIndirect.mapped != nullptr) {
        auto* shadowArgs = static_cast<uint32_t*>(scene->shadowIndirect.mapped);
        shadowArgs[flight * 16u + 12u] = scene->instanceCount;
        gpuBufferFlush(scene->shadowIndirect, device, 0, scene->shadowIndirect.size);
    }
    if (scene->gpuTime.pool == VK_NULL_HANDLE) {
        (void)gpuProfilerCreate(scene->gpuTime, device);
    }
    bool taaBound = false;
    if (taaLive && scene->tone.history[0].image.image != VK_NULL_HANDLE) {
        const uint32_t curr = flight & 1u;
        const uint32_t prev = curr ^ 1u;
        taaBound = heapWriteImageFlight(heaps, device, flight, 866u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, scene->tone.history[prev].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
            && heapWriteImageFlight(heaps, device, flight, 867u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, scene->tone.history[curr].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
            && heapWriteImageFlight(heaps, device, flight, 868u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, scene->tone.history[curr].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    heapBind(cmd, scene->gpuHeap, flight);
    if (scene->texPublish > 0) {
        const GpuBuffer& heapBuf = scene->gpuHeap.resources[flight];
        rhiBufferBarrier(cmd, heapBuf.buffer, 0, heapBuf.size,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT);
        const uint32_t from = scene->texPublishBase;
        const uint32_t to = scene->textureSlots < scene->textures.size()
            ? scene->textureSlots
            : static_cast<uint32_t>(scene->textures.size());
        for (uint32_t i = from; i < to; ++i) {
            const GpuImage& pic = scene->textures[i];
            const uint32_t mips = pic.mips == 0u ? 1u : pic.mips;
            rhiImageBarrierMips(cmd, pic.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                0, mips);
        }
        scene->texPublish = static_cast<uint8_t>(scene->texPublish - 1u);
    }
    gpuProfilerBegin(scene->gpuTime, device, cmd, flight);
    char zoneText[3584];
    (void)gpuProfilerFormat(scene->gpuTime, zoneText, sizeof(zoneText));
    static std::chrono::steady_clock::time_point gpuLog{};
    const auto gpuNow = std::chrono::steady_clock::now();
    const bool logFrame = scene->gpuTime.count > 0 && gpuNow - gpuLog >= std::chrono::seconds(2);
    if (logFrame) {
        gpuLog = gpuNow;
    }
    bufferBarrierHostToShader(cmd, scene->postBuf.buffer, scene->postBuf.size);
    bufferBarrierHostToShader(cmd, scene->frameBuf.buffer, scene->frameBuf.size);
    bufferBarrierHostToShader(cmd, scene->sunBuf.buffer, scene->sunBuf.size);
    bufferBarrierHostToShader(cmd, scene->atmBuf.buffer, scene->atmBuf.size);
    bufferBarrierHostToShader(cmd, scene->lightBuf.buffer, scene->lightBuf.size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const RenderContext ctx{cmd, device, scene->gpuHeap, scene->gpuTime, scene->extent, flight, scene->freeze, gpu, lit, knob};
    framePassClusters(ctx, scene->clusters, scene->cellBuf, scene->lightIndex);
    CullBuffers cullBufs{scene->shadowIndirect, scene->shadowVisible, scene->indirect, scene->candidates, scene->visible, scene->instanceCount, strideU, sunKeep};
    CullResults culled{};
    culled.draw = VisInstanceDraw{scene->instances.address, scene->visible.address, scene->visibleCount};
    framePassCull(ctx, *scene, cullBufs, culled);
    GpuDebugStats stats{};
    stats.visibleInstances = scene->visibleCount;
    stats.culledInstances = scene->instanceCount - scene->visibleCount;
    stats.visibleMeshlets = scene->visibleCount;
    stats.activeLights = scene->world.lightCount;
    stats.fps = scene->fps;
    stats.frameTimeMs = scene->fps > 1.0f ? 1000.0f / scene->fps : 0.0f;
    debugFormatStats(stats, scene->title, sizeof(scene->title));
    const float (&sunC)[3] = u.sunCentre;
    SunPassData sunData{scene->shadowDepth[flight], scene->instances, scene->shadowVisible, scene->shadowIndirect, scene->shadowStamp, scene->shadowStampValid, scene->shadowStampCount, strideU, sunC, splits};
    framePassSun(ctx, *scene, scene->vis, sunData);
    framePassPoints(ctx, scene->vis, scene->cube, scene->pointDepth, scene->cubeColor, scene->cubeDepth, scene->world.lightCount,
        scene->pointCount, scene->cubeCount, scene->pointCursor, scene->pointReady, scene->cubeReady, scene->cubeFace);
    if (!scene->cubeReady) {
        scene->cubeBaked = false;
    } else if (!scene->cubeBaked) {
        scene->cubeBaked = true;
    }
    framePassVis(ctx, scene->vis, scene->occl, scene->hiz, scene->hizMax[flight], scene->indirect, scene->candidates, scene->visible,
        scene->instanceCount, culled.twoPhase, scene->showHiz, culled.draw, culled.lateDraw);
    const bool restirOn = (scene->passMask & kPassRestir) != 0u && scene->restir.di.handle != VK_NULL_HANDLE;
    const bool gtaoOn = (scene->passMask & 524288u) == 0u && scene->lightProfile != 2u && (knob.flags.passMask & 16u) != 0u && scene->hud.gtao > 0.001f;
    framePassShade(ctx, scene->shade, scene->sky, scene->gtao, scene->vis.targets.vis, scene->vis.targets.depth, gtaoOn, scene->probeBuf.buffer, scene->probeBuf.size, restirOn, pose.fovYRad);
    const VkDeviceAddress tlas = scene->radiance.geometry ? scene->radiance.tlasAddress : 0;
    const bool worldSharc = (scene->passMask & kPassSharc) != 0u;
    const bool worldRc = !worldSharc && (scene->passMask & 128u) != 0u && (scene->passMask & 262144u) != 0u;
    const bool worldVx = !worldSharc && (scene->passMask & 524288u) != 0u && (scene->passMask & 262144u) != 0u && !worldRc;
    if (!restirOn) {
        restirNote(scene->restir, scene->lightProfile, (scene->passMask & kPassRestir) != 0u ? "нет шейдера" : "выкл");
    }
    restirStatReset(cmd, scene->restir, device, flight);
    if (restirOn) {
        const void* gpuLights = lightSrc;
        if (scene->lightBuf.mapped != nullptr) {
            gpuLights = static_cast<const uint8_t*>(scene->lightBuf.mapped) + sizeof(LightGpuData) * kLightCap * flight;
        }
        framePassRestir(ctx, scene->restir, gpu, gpuLights, lightCount, scene->passMask, tlas);
    }
    if (worldSharc) {
        framePassSharc(ctx, scene->sharc, scene->shade.hdrA, scene->giGain, lightCount, tlas, scene->restir, restirOn ? 1u : 0u, scene->sharcDenoise ? 1u : 0u);
    } else if (worldVx) {
        framePassVoxel(ctx, scene->voxel, scene->shade.hdrA, true, scene->instanceCount, propMoved);
    } else {
        framePassRadiance(ctx, scene->radiance, scene->shade.hdrA, worldRc, scene->rcRays, scene->rcSpacing, scene->rcIntensity, scene->rcMax, scene->rcSun, scene->rcBounce);
    }
    framePassShadeLate(ctx, scene->shade, scene->sky, scene->fog);
    particleRecord(cmd, device, scene->particles, scene->shade.hdrA, scene->extent, flight, scene->simDt);
    rainRecord(cmd, scene->rain, scene->shade.hdrA, scene->extent, flight, scene->simDt);
    restirStatFlush(cmd, scene->restir, flight);
    if (scene->restir.lastLog[0] != '\0') {
        std::snprintf(scene->gpuText, sizeof(scene->gpuText), "%s\n%s", scene->restir.lastLog, zoneText);
    } else {
        std::snprintf(scene->gpuText, sizeof(scene->gpuText), "%s", zoneText);
    }
    {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 32 < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\ncpu record %5.2f", static_cast<double>(scene->cpuRecordMs));
        }
    }
    const bool bloomOn = (knob.flags.passMask & 32u) != 0u && scene->hud.bloom > 0.001f;
    if (fsrLive) {
        const float dt = scene->fps > 1.0f ? 1.0f / scene->fps : 1.0f / 60.0f;
        fsrRecord(cmd, scene->fsr, device, heaps, flight, scene->vis.targets.depth, scene->shade.hdrA, dt, pose.fovYRad, pose.zn, unjitVp, jitInv);
    } else {
        fsrRestoreColor(scene->fsr, device, heaps, flight, scene->shade.hdrA.image);
        scene->fsr.lastLog[0] = '\0';
    }
    framePassTonemap(ctx, scene->tone, scene->shade.hdrA, scene->shade.hdrB, swap, frame, bloomOn, fsrLive, taaLive && taaBound);
    if (scene->fsr.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->fsr.lastLog);
        }
    }
    if (scene->gtao.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->gtao.lastLog);
        }
    }
    if (worldSharc && scene->sharc.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->sharc.lastLog);
        }
    }
    if (logFrame) {
        spdlog::info("frame {:.1f} ms ({:.1f} fps)\n{}", scene->fps > 1.0f ? 1000.0f / scene->fps : 0.0, static_cast<double>(scene->fps), scene->gpuText);
        if (scene->fsr.lastLog[0] != '\0') {
            spdlog::info("{}", scene->fsr.lastLog);
        }
        if (scene->gtao.lastLog[0] != '\0') {
            spdlog::info("{}", scene->gtao.lastLog);
        }
    }
    gizmoPassRecord(cmd, device, swap, frame, scene->gizmo, scene->actors);
}

} // namespace burnhope
