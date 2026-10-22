#include "audio/Audio.hpp"

#include "io/File.hpp"
#include "platform/PlatformUtil.hpp"

#include <phonon.h>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <spdlog/spdlog.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace burnhope {

constexpr int32_t kRate = 48000;
constexpr int32_t kFrame = 512;
constexpr uint32_t kRing = 16384;

float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float len3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

bool mapLeaf(const char* leaf, MappedFile& mapped, char* path, size_t cap) {
    char rel[3][160];
    std::snprintf(rel[0], sizeof(rel[0]), "assets/%s", leaf);
    std::snprintf(rel[1], sizeof(rel[1]), "../assets/%s", leaf);
    std::snprintf(rel[2], sizeof(rel[2]), "../../assets/%s", leaf);
    for (const char* item : rel) {
        if (fileMapReadOnly(item, &mapped)) {
            strCopyCap(path, cap, item);
            return true;
        }
    }
    char exe[512];
    if (!platformExePath(exe, sizeof(exe))) {
        return false;
    }
    char dir[512];
    strCopyCap(dir, sizeof(dir), exe);
    for (int i = 0; i < 6; ++i) {
        char parent[512];
        if (!pathParent(dir, parent, sizeof(parent))) {
            break;
        }
        strCopyCap(dir, sizeof(dir), parent);
        std::snprintf(path, cap, "%s/assets/%s", dir, leaf);
        if (fileMapReadOnly(path, &mapped)) {
            return true;
        }
    }
    return false;
}

bool loadLeaf(const char* leaf, float*& pcm, uint32_t& frames) {
    pcm = nullptr;
    frames = 0;
    MappedFile mapped{};
    char path[512];
    if (!mapLeaf(leaf, mapped, path, sizeof(path))) {
        spdlog::warn("audio missing {}", leaf);
        return false;
    }
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, kRate);
    ma_uint64 count = 0;
    void* data = nullptr;
    const ma_result result = ma_decode_memory(mapped.data, mapped.size, &cfg, &count, &data);
    fileUnmap(&mapped);
    if (result != MA_SUCCESS || data == nullptr || count == 0) {
        if (data != nullptr) {
            ma_free(data, nullptr);
        }
        spdlog::warn("audio decode {}", leaf);
        return false;
    }
    pcm = static_cast<float*>(data);
    frames = static_cast<uint32_t>(count > 0xffffffffu ? 0xffffffffu : count);
    spdlog::info("audio {} {} frames", path, frames);
    return true;
}

struct AudioWorld {
    ma_device device{};
    bool deviceOn = false;
    float* amb = nullptr;
    float* roof = nullptr;
    uint32_t ambFrames = 0;
    uint32_t roofFrames = 0;
    uint32_t ambCursor = 0;
    uint32_t roofCursor = 0;
    uint32_t spatCursor = 0;
    std::atomic<float> targetOpen{1.0f};
    std::atomic<float> targetAmount{3.0f};
    float slewOpen = 1.0f;
    float slewVol = 1.0f;
    float ring[kRing * 2]{};
    std::atomic<uint32_t> ringW{0};
    std::atomic<uint32_t> ringR{0};
    AudioListener ear{};
    IPLContext context = nullptr;
    IPLHRTF hrtf = nullptr;
    IPLSimulator simulator = nullptr;
    IPLScene scene = nullptr;
    IPLStaticMesh mesh = nullptr;
    IPLSource source = nullptr;
    IPLDirectEffect directFx = nullptr;
    IPLBinauralEffect binFx = nullptr;
    IPLReflectionEffect reflFx = nullptr;
    IPLAmbisonicsBinauralEffect ambiFx = nullptr;
    IPLDirectEffectParams direct{};
    IPLReflectionEffectParams refl{};
    bool reflReady = false;
    float reflectWait = 0.0f;
    IPLVector3 toSource{0.0f, 1.0f, 0.0f};
    float chMono[kFrame]{};
    float chDirect[kFrame]{};
    float chAmbi[4][kFrame]{};
    float chEar[2][kFrame]{};
    float chRefl[2][kFrame]{};
    float* pMono = nullptr;
    float* pDirect = nullptr;
    float* pAmbi[4]{};
    float* pEar[2]{};
    float* pRefl[2]{};
};

void pullStereo(const float* pcm, uint32_t frames, uint32_t& cursor, float& left, float& right) {
    if (pcm == nullptr || frames == 0) {
        left = 0.0f;
        right = 0.0f;
        return;
    }
    const uint32_t at = cursor % frames;
    cursor = at + 1u < frames ? at + 1u : 0u;
    left = pcm[at * 2u];
    right = pcm[at * 2u + 1u];
}

bool ringPop(AudioWorld& world, float& left, float& right) {
    const uint32_t rd = world.ringR.load(std::memory_order_relaxed);
    const uint32_t wr = world.ringW.load(std::memory_order_acquire);
    if (rd == wr) {
        return false;
    }
    const uint32_t at = rd & (kRing - 1u);
    left = world.ring[at * 2u];
    right = world.ring[at * 2u + 1u];
    world.ringR.store(rd + 1u, std::memory_order_release);
    return true;
}

void playback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* world = static_cast<AudioWorld*>(device->pUserData);
    auto* dst = static_cast<float*>(output);
    if (world == nullptr) {
        std::memset(dst, 0, sizeof(float) * frames * 2u);
        return;
    }
    const float wantOpen = clampf(world->targetOpen.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float wantVol = clampf(world->targetAmount.load(std::memory_order_relaxed), 0.0f, 1.0f);
    for (ma_uint32 i = 0; i < frames; ++i) {
        world->slewOpen += (wantOpen - world->slewOpen) * 0.000012f;
        world->slewVol += (wantVol - world->slewVol) * 0.00008f;
        float ambL = 0.0f;
        float ambR = 0.0f;
        float roofL = 0.0f;
        float roofR = 0.0f;
        pullStereo(world->amb, world->ambFrames, world->ambCursor, ambL, ambR);
        pullStereo(world->roof, world->roofFrames, world->roofCursor, roofL, roofR);
        float spatL = 0.0f;
        float spatR = 0.0f;
        ringPop(*world, spatL, spatR);
        const float open = world->slewOpen;
        const float vol = world->slewVol;
        const float weather = 0.36f + open * 0.64f;
        const float cover = 0.16f + (1.0f - open) * 0.84f;
        float left = (ambL * weather * 0.72f + roofL * cover * 0.78f) * vol + spatL * vol * 0.42f;
        float right = (ambR * weather * 0.72f + roofR * cover * 0.78f) * vol + spatR * vol * 0.42f;
        dst[i * 2u] = clampf(left, -1.2f, 1.2f);
        dst[i * 2u + 1u] = clampf(right, -1.2f, 1.2f);
    }
}

void pullMono(const float* pcm, uint32_t frames, uint32_t& cursor, float* dst, int32_t count) {
    if (pcm == nullptr || frames == 0) {
        std::memset(dst, 0, sizeof(float) * static_cast<size_t>(count));
        return;
    }
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t at = cursor % frames;
        cursor = at + 1u < frames ? at + 1u : 0u;
        dst[i] = (pcm[at * 2u] + pcm[at * 2u + 1u]) * 0.5f;
    }
}

bool ringPush(AudioWorld& world, const float* stereo, int32_t frames) {
    const uint32_t wr = world.ringW.load(std::memory_order_relaxed);
    const uint32_t rd = world.ringR.load(std::memory_order_acquire);
    if (wr - rd + static_cast<uint32_t>(frames) >= kRing) {
        return false;
    }
    for (int32_t i = 0; i < frames; ++i) {
        const uint32_t at = (wr + static_cast<uint32_t>(i)) & (kRing - 1u);
        world.ring[at * 2u] = stereo[i * 2];
        world.ring[at * 2u + 1u] = stereo[i * 2 + 1];
    }
    world.ringW.store(wr + static_cast<uint32_t>(frames), std::memory_order_release);
    return true;
}

IPLCoordinateSpace3 listenerSpace(const AudioListener& ear) {
    IPLCoordinateSpace3 space{};
    float ax = ear.ahead[0];
    float ay = ear.ahead[1];
    float az = ear.ahead[2];
    const float al = len3(ax, ay, az);
    if (al > 1.0e-4f) {
        ax /= al;
        ay /= al;
        az /= al;
    } else {
        ax = 0.0f;
        ay = 0.0f;
        az = -1.0f;
    }
    float ux = ear.up[0];
    float uy = ear.up[1];
    float uz = ear.up[2];
    const float ul = len3(ux, uy, uz);
    if (ul > 1.0e-4f) {
        ux /= ul;
        uy /= ul;
        uz /= ul;
    } else {
        ux = 0.0f;
        uy = 1.0f;
        uz = 0.0f;
    }
    float rx = ay * uz - az * uy;
    float ry = az * ux - ax * uz;
    float rz = ax * uy - ay * ux;
    const float rl = len3(rx, ry, rz);
    if (rl < 1.0e-4f) {
        rx = 1.0f;
        ry = 0.0f;
        rz = 0.0f;
    } else {
        rx /= rl;
        ry /= rl;
        rz /= rl;
    }
    space.right = {rx, ry, rz};
    space.up = {ry * az - rz * ay, rz * ax - rx * az, rx * ay - ry * ax};
    space.ahead = {ax, ay, az};
    space.origin = {ear.origin[0], ear.origin[1], ear.origin[2]};
    return space;
}

bool renderBlock(AudioWorld& world) {
    pullMono(world.amb, world.ambFrames, world.spatCursor, world.chMono, kFrame);
    if (world.directFx == nullptr || world.binFx == nullptr) {
        float stereo[kFrame * 2];
        for (int32_t i = 0; i < kFrame; ++i) {
            stereo[i * 2] = world.chMono[i] * 0.15f;
            stereo[i * 2 + 1] = world.chMono[i] * 0.15f;
        }
        return ringPush(world, stereo, kFrame);
    }
    IPLAudioBuffer mono{1, kFrame, &world.pMono};
    IPLAudioBuffer directed{1, kFrame, &world.pDirect};
    IPLDirectEffectParams direct = world.direct;
    direct.flags = static_cast<IPLDirectEffectFlags>(
        IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION
        | IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
    direct.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
    iplDirectEffectApply(world.directFx, &direct, &mono, &directed);
    IPLBinauralEffectParams bin{};
    bin.direction = world.toSource;
    bin.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
    bin.spatialBlend = 1.0f;
    bin.hrtf = world.hrtf;
    IPLAudioBuffer ears{2, kFrame, world.pEar};
    iplBinauralEffectApply(world.binFx, &bin, &directed, &ears);
    if (world.reflReady && world.reflFx != nullptr && world.ambiFx != nullptr && world.refl.ir != nullptr) {
        IPLAudioBuffer ambi{4, kFrame, world.pAmbi};
        IPLReflectionEffectParams refl = world.refl;
        refl.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
        iplReflectionEffectApply(world.reflFx, &refl, &mono, &ambi, nullptr);
        IPLAmbisonicsBinauralEffectParams amb{};
        amb.hrtf = world.hrtf;
        amb.order = 1;
        IPLAudioBuffer wet{2, kFrame, world.pRefl};
        iplAmbisonicsBinauralEffectApply(world.ambiFx, &amb, &ambi, &wet);
        float stereo[kFrame * 2];
        for (int32_t i = 0; i < kFrame; ++i) {
            stereo[i * 2] = world.chEar[0][i] + world.chRefl[0][i] * 0.5f;
            stereo[i * 2 + 1] = world.chEar[1][i] + world.chRefl[1][i] * 0.5f;
        }
        return ringPush(world, stereo, kFrame);
    }
    float stereo[kFrame * 2];
    for (int32_t i = 0; i < kFrame; ++i) {
        stereo[i * 2] = world.chEar[0][i];
        stereo[i * 2 + 1] = world.chEar[1][i];
    }
    return ringPush(world, stereo, kFrame);
}

void releasePhonon(AudioWorld& world) {
    if (world.ambiFx != nullptr) {
        iplAmbisonicsBinauralEffectRelease(&world.ambiFx);
    }
    if (world.binFx != nullptr) {
        iplBinauralEffectRelease(&world.binFx);
    }
    if (world.reflFx != nullptr) {
        iplReflectionEffectRelease(&world.reflFx);
    }
    if (world.directFx != nullptr) {
        iplDirectEffectRelease(&world.directFx);
    }
    if (world.source != nullptr) {
        if (world.simulator != nullptr) {
            iplSourceRemove(world.source, world.simulator);
        }
        iplSourceRelease(&world.source);
    }
    if (world.mesh != nullptr) {
        iplStaticMeshRelease(&world.mesh);
    }
    if (world.scene != nullptr) {
        iplSceneRelease(&world.scene);
    }
    if (world.simulator != nullptr) {
        iplSimulatorRelease(&world.simulator);
    }
    if (world.hrtf != nullptr) {
        iplHRTFRelease(&world.hrtf);
    }
    if (world.context != nullptr) {
        iplContextRelease(&world.context);
    }
    world.reflReady = false;
}

bool openPhonon(AudioWorld& world) {
    IPLContextSettings contextSettings{};
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
    if (iplContextCreate(&contextSettings, &world.context) != IPL_STATUS_SUCCESS) {
        return false;
    }
    IPLAudioSettings audio{};
    audio.samplingRate = kRate;
    audio.frameSize = kFrame;
    IPLHRTFSettings hrtfSettings{};
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1.0f;
    hrtfSettings.normType = IPL_HRTFNORMTYPE_NONE;
    if (iplHRTFCreate(world.context, &audio, &hrtfSettings, &world.hrtf) != IPL_STATUS_SUCCESS) {
        return false;
    }
    IPLSimulationSettings sim{};
    sim.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    sim.sceneType = IPL_SCENETYPE_DEFAULT;
    sim.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    sim.maxNumOcclusionSamples = 16;
    sim.maxNumRays = 1024;
    sim.numDiffuseSamples = 16;
    sim.maxDuration = 1.0f;
    sim.maxOrder = 1;
    sim.maxNumSources = 4;
    sim.numThreads = 2;
    sim.rayBatchSize = 16;
    sim.numVisSamples = 1;
    sim.samplingRate = kRate;
    sim.frameSize = kFrame;
    if (iplSimulatorCreate(world.context, &sim, &world.simulator) != IPL_STATUS_SUCCESS) {
        return false;
    }
    IPLSceneSettings sceneSettings{};
    sceneSettings.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(world.context, &sceneSettings, &world.scene) != IPL_STATUS_SUCCESS) {
        return false;
    }
    iplSceneCommit(world.scene);
    iplSimulatorSetScene(world.simulator, world.scene);
    iplSimulatorCommit(world.simulator);
    IPLSourceSettings sourceSettings{};
    sourceSettings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    if (iplSourceCreate(world.simulator, &sourceSettings, &world.source) != IPL_STATUS_SUCCESS) {
        return false;
    }
    iplSourceAdd(world.source, world.simulator);
    IPLDirectEffectSettings directSettings{};
    directSettings.numChannels = 1;
    if (iplDirectEffectCreate(world.context, &audio, &directSettings, &world.directFx) != IPL_STATUS_SUCCESS) {
        return false;
    }
    IPLBinauralEffectSettings binSettings{};
    binSettings.hrtf = world.hrtf;
    if (iplBinauralEffectCreate(world.context, &audio, &binSettings, &world.binFx) != IPL_STATUS_SUCCESS) {
        return false;
    }
    IPLReflectionEffectSettings reflSettings{};
    reflSettings.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    reflSettings.irSize = kRate;
    reflSettings.numChannels = 4;
    if (iplReflectionEffectCreate(world.context, &audio, &reflSettings, &world.reflFx) != IPL_STATUS_SUCCESS) {
        return false;
    }
    IPLAmbisonicsBinauralEffectSettings ambiSettings{};
    ambiSettings.hrtf = world.hrtf;
    ambiSettings.maxOrder = 1;
    if (iplAmbisonicsBinauralEffectCreate(world.context, &audio, &ambiSettings, &world.ambiFx) != IPL_STATUS_SUCCESS) {
        return false;
    }
    world.direct.distanceAttenuation = 1.0f;
    world.direct.airAbsorption[0] = world.direct.airAbsorption[1] = world.direct.airAbsorption[2] = 1.0f;
    world.direct.transmission[0] = world.direct.transmission[1] = world.direct.transmission[2] = 1.0f;
    world.direct.occlusion = 0.0f;
    return true;
}

AudioWorld* audioCreate() {
    auto* world = new AudioWorld{};
    world->pMono = world->chMono;
    world->pDirect = world->chDirect;
    for (int i = 0; i < 4; ++i) {
        world->pAmbi[i] = world->chAmbi[i];
    }
    world->pEar[0] = world->chEar[0];
    world->pEar[1] = world->chEar[1];
    world->pRefl[0] = world->chRefl[0];
    world->pRefl[1] = world->chRefl[1];
    loadLeaf("ambient rain.wav", world->amb, world->ambFrames);
    loadLeaf("raining-on-roof.wav", world->roof, world->roofFrames);
    if (!openPhonon(*world)) {
        spdlog::error("Steam Audio init failed");
        releasePhonon(*world);
    } else {
        spdlog::info("Steam Audio ready");
    }
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kRate;
    config.periodSizeInFrames = kFrame;
    config.dataCallback = playback;
    config.pUserData = world;
    if (ma_device_init(nullptr, &config, &world->device) != MA_SUCCESS) {
        spdlog::error("miniaudio device failed");
        return world;
    }
    world->deviceOn = true;
    if (ma_device_start(&world->device) != MA_SUCCESS) {
        spdlog::error("miniaudio start failed");
    }
    return world;
}

void audioDestroy(AudioWorld* world) {
    if (world == nullptr) {
        return;
    }
    if (world->deviceOn) {
        ma_device_stop(&world->device);
        ma_device_uninit(&world->device);
        world->deviceOn = false;
    }
    releasePhonon(*world);
    if (world->amb != nullptr) {
        ma_free(world->amb, nullptr);
    }
    if (world->roof != nullptr) {
        ma_free(world->roof, nullptr);
    }
    delete world;
}

bool audioSetMesh(AudioWorld* world, const AudioMesh& mesh) {
    if (world == nullptr || world->context == nullptr || world->simulator == nullptr) {
        return false;
    }
    if (world->mesh != nullptr) {
        iplStaticMeshRelease(&world->mesh);
    }
    if (world->scene != nullptr) {
        iplSceneRelease(&world->scene);
        world->scene = nullptr;
    }
    IPLSceneSettings sceneSettings{};
    sceneSettings.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(world->context, &sceneSettings, &world->scene) != IPL_STATUS_SUCCESS) {
        return false;
    }
    if (mesh.triangleCount == 0 || mesh.xyz == nullptr || mesh.indices == nullptr) {
        iplSceneCommit(world->scene);
        iplSimulatorSetScene(world->simulator, world->scene);
        iplSimulatorCommit(world->simulator);
        return true;
    }
    std::vector<IPLVector3> verts(mesh.vertexCount);
    for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
        verts[i] = {mesh.xyz[i * 3u], mesh.xyz[i * 3u + 1u], mesh.xyz[i * 3u + 2u]};
    }
    std::vector<IPLTriangle> tris(mesh.triangleCount);
    std::vector<IPLint32> mats(mesh.triangleCount);
    for (uint32_t i = 0; i < mesh.triangleCount; ++i) {
        tris[i].indices[0] = mesh.indices[i * 3u];
        tris[i].indices[1] = mesh.indices[i * 3u + 1u];
        tris[i].indices[2] = mesh.indices[i * 3u + 2u];
        mats[i] = mesh.materialIndex != nullptr ? mesh.materialIndex[i] : 0;
    }
    IPLMaterial materials[2]{};
    materials[0].absorption[0] = 0.05f;
    materials[0].absorption[1] = 0.07f;
    materials[0].absorption[2] = 0.08f;
    materials[0].scattering = 0.05f;
    materials[0].transmission[0] = 0.015f;
    materials[0].transmission[1] = 0.002f;
    materials[0].transmission[2] = 0.001f;
    materials[1].absorption[0] = 0.24f;
    materials[1].absorption[1] = 0.50f;
    materials[1].absorption[2] = 0.70f;
    materials[1].scattering = 0.15f;
    materials[1].transmission[0] = 0.02f;
    materials[1].transmission[1] = 0.008f;
    materials[1].transmission[2] = 0.003f;
    IPLStaticMeshSettings settings{};
    settings.numVertices = static_cast<IPLint32>(verts.size());
    settings.numTriangles = static_cast<IPLint32>(tris.size());
    settings.numMaterials = 2;
    settings.vertices = verts.data();
    settings.triangles = tris.data();
    settings.materialIndices = mats.data();
    settings.materials = materials;
    if (iplStaticMeshCreate(world->scene, &settings, &world->mesh) != IPL_STATUS_SUCCESS) {
        spdlog::error("Steam Audio mesh failed");
        return false;
    }
    iplStaticMeshAdd(world->mesh, world->scene);
    iplSceneCommit(world->scene);
    iplSimulatorSetScene(world->simulator, world->scene);
    iplSimulatorCommit(world->simulator);
    world->reflReady = false;
    return true;
}

void audioSetListener(AudioWorld* world, const AudioListener& ear) {
    if (world == nullptr) {
        return;
    }
    world->ear = ear;
}

void audioSetRain(AudioWorld* world, float openSky, float amount) {
    if (world == nullptr) {
        return;
    }
    world->targetOpen.store(clampf(openSky, 0.0f, 1.0f), std::memory_order_relaxed);
    world->targetAmount.store(clampf(amount, 0.0f, 4.0f), std::memory_order_relaxed);
}

void audioTick(AudioWorld* world, float dt) {
    if (world == nullptr || !world->deviceOn) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    if (world->simulator != nullptr && world->source != nullptr) {
        const IPLCoordinateSpace3 listener = listenerSpace(world->ear);
        IPLCoordinateSpace3 source{};
        source.origin = {listener.origin.x, listener.origin.y + 8.0f, listener.origin.z};
        source.ahead = {0.0f, -1.0f, 0.0f};
        source.up = {0.0f, 0.0f, 1.0f};
        source.right = {1.0f, 0.0f, 0.0f};
        IPLSimulationInputs inputs{};
        inputs.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
        inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
            IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION | IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION
            | IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
        inputs.source = source;
        inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
        inputs.distanceAttenuationModel.minDistance = 12.0f;
        inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
        inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
        inputs.occlusionRadius = 1.4f;
        inputs.numOcclusionSamples = 8;
        inputs.numTransmissionRays = 4;
        inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
        inputs.hybridReverbTransitionTime = 0.22f;
        inputs.hybridReverbOverlapPercent = 0.25f;
        iplSourceSetInputs(world->source, inputs.flags, &inputs);
        IPLSimulationSharedInputs shared{};
        shared.listener = listener;
        shared.numRays = 768;
        shared.numBounces = 4;
        shared.duration = 1.0f;
        shared.order = 1;
        shared.irradianceMinDistance = 1.0f;
        iplSimulatorSetSharedInputs(world->simulator, inputs.flags, &shared);
        iplSimulatorRunDirect(world->simulator);
        world->reflectWait += dt;
        if (world->reflectWait > 0.12f) {
            world->reflectWait = 0.0f;
            iplSimulatorRunReflections(world->simulator);
            IPLSimulationOutputs reflected{};
            iplSourceGetOutputs(world->source, IPL_SIMULATIONFLAGS_REFLECTIONS, &reflected);
            if (reflected.reflections.ir != nullptr) {
                world->refl = reflected.reflections;
                world->reflReady = true;
            }
        }
        IPLSimulationOutputs direct{};
        iplSourceGetOutputs(world->source, IPL_SIMULATIONFLAGS_DIRECT, &direct);
        world->direct = direct.direct;
        world->toSource = iplCalculateRelativeDirection(
            world->context, source.origin, listener.origin, listener.ahead, listener.up);
    }
    const uint32_t need = static_cast<uint32_t>(dt * static_cast<float>(kRate)) + static_cast<uint32_t>(kFrame);
    uint32_t blocks = (need + static_cast<uint32_t>(kFrame) - 1u) / static_cast<uint32_t>(kFrame);
    if (blocks < 1u) {
        blocks = 1u;
    }
    if (blocks > 3u) {
        blocks = 3u;
    }
    for (uint32_t i = 0; i < blocks; ++i) {
        if (!renderBlock(*world)) {
            break;
        }
    }
}

} // namespace burnhope
