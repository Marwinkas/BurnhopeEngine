#include "color/Color.hpp"
#include "render/Weather.hpp"
#include "render/SceneFrameImpl.hpp"
#include "gpu_scene/LightMath.hpp"
#include "gpu_scene/PointShadow.hpp"
#include "gpu_scene/SunShadow.hpp"

#include <cstddef>
#include <type_traits>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace burnhope {

namespace {



float clampRange(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

} // namespace

static_assert(std::is_trivially_copyable_v<HudPost>);
static_assert(std::is_trivially_copyable_v<PostPack>);
static_assert(sizeof(PostPack) == 16 * sizeof(float));
static_assert(offsetof(HudPost, gradeFlags) - offsetof(HudPost, exposure) == sizeof(PostPack) - sizeof(float));
static_assert(sizeof(HudPost) == kScPostN * sizeof(float));

HudPost sceneBlockPost(const SceneBlock& block) {
    HudPost post{};
    const std::span<const float> s = sceneBlockSpan(block, kScPost, kScPostN);
    if (s.size() != kScPostN) {
        return post;
    }
    std::memcpy(static_cast<void*>(&post), s.data(), sizeof(HudPost));
    return post;
}

SceneTweaks sceneBlockTweaks(const SceneBlock& block) {
    SceneTweaks tweaks{};
    const std::span<const float> gtao = sceneBlockSpan(block, kScGtao, kScGtaoN);
    const std::span<const float> ssr = sceneBlockSpan(block, kScSsr, kScSsrN);
    const std::span<const float> sscs = sceneBlockSpan(block, kScSscs, kScSscsN);
    const std::span<const float> ssrc = sceneBlockSpan(block, kScSsrc, kScSsrcN);
    const std::span<const float> rc = sceneBlockSpan(block, kScRc, kScRcN);
    const std::span<const float> restir = sceneBlockSpan(block, kScRestir, kScRestirN);
    const std::span<const float> fog = sceneBlockSpan(block, kScFog, kScFogN);
    tweaks.forceLod = static_cast<int32_t>(sceneBlockAt(block, kScForceLod));
    tweaks.passMask = block.passMask;
    tweaks.gtaoRadius = gtao[0];
    tweaks.gtaoSteps = gtao[1];
    tweaks.gtaoSlices = gtao[2];
    tweaks.gtaoPower = gtao[3];
    tweaks.gtaoFalloff = gtao[4];
    tweaks.gtaoDenoise = gtao[5];
    tweaks.ssrCutoff = ssr[0];
    tweaks.ssrSteps = ssr[1];
    tweaks.ssrStride = ssr[2];
    tweaks.ssrThick = ssr[3];
    tweaks.ssrMetal = ssr[4];
    tweaks.ssrRt = ssr[5];
    tweaks.ssrEdge = ssr[6];
    tweaks.ssrMax = ssr[7];
    tweaks.ssrBias = ssr[8];
    tweaks.sscsReach = sscs[0];
    tweaks.sscsThick = sscs[1];
    tweaks.sscsSteps = sscs[2];
    tweaks.sscsPx = sscs[3];
    tweaks.ssrcBase = ssrc[0];
    tweaks.ssrcThick = ssrc[1];
    tweaks.ssrcCap = ssrc[2];
    tweaks.ssrcSteps = ssrc[3];
    tweaks.rcRays = rc[0];
    tweaks.rcSpacing = rc[1];
    tweaks.rcIntensity = rc[2];
    tweaks.rcMax = rc[3];
    tweaks.rcSun = rc[4];
    tweaks.rcBounce = rc[5];
    tweaks.giGain = rc[6];
    tweaks.sharcDenoise = sceneBlockFlag(block, kFlSharcDenoise) != 0;
    tweaks.restirLocal = restir[0];
    tweaks.restirSpatial = restir[1];
    tweaks.restirRadius = restir[2];
    tweaks.restirHistory = restir[3];
    tweaks.restirBoil = restir[4];
    tweaks.restirTemporalDepth = restir[5];
    tweaks.restirTemporalNormal = restir[6];
    tweaks.restirTemporalBias = restir[7];
    tweaks.restirSpatialDepth = restir[8];
    tweaks.restirSpatialNormal = restir[9];
    tweaks.restirSpatialBias = restir[10];
    tweaks.restirSpatialBoost = restir[11];
    tweaks.restirSunDisk = restir[12];
    tweaks.restirSunRays = restir[13];
    tweaks.restirShadowRays = restir[14];
    tweaks.restirShadowRange = restir[15];
    tweaks.restirShadowBias = restir[16];
    tweaks.restirPermTh = restir[17];
    tweaks.restirTarget = restir[18];
    tweaks.restirTemporal = sceneBlockFlag(block, kFlRestirTemporal) != 0;
    tweaks.restirSpatialOn = sceneBlockFlag(block, kFlRestirSpatial) != 0;
    tweaks.restirBoilOn = sceneBlockFlag(block, kFlRestirBoil) != 0;
    tweaks.restirInitVis = sceneBlockFlag(block, kFlRestirInitVis) != 0;
    tweaks.restirPerm = sceneBlockFlag(block, kFlRestirPerm) != 0;
    tweaks.restirShortcut = sceneBlockFlag(block, kFlRestirShortcut) != 0;
    tweaks.restirMaterial = sceneBlockFlag(block, kFlRestirMaterial) != 0;
    tweaks.restirNaive = sceneBlockFlag(block, kFlRestirNaive) != 0;
    tweaks.restirSunOn = sceneBlockFlag(block, kFlRestirSun) != 0;
    tweaks.fsrQuality = sceneBlockFlag(block, kFlFsrQ);
    tweaks.fsrSharpness = sceneBlockAt(block, kScFsrSharpness);
    tweaks.lightProfile = sceneBlockFlag(block, kFlGi);
    tweaks.bloomThreshold = sceneBlockAt(block, kScBloomThreshold);
    tweaks.fogDensity = fog[0];
    tweaks.fogHeight = fog[1];
    tweaks.fogScatter = fog[2];
    tweaks.fogFalloff = fog[3];
    tweaks.fogR = fog[4];
    tweaks.fogG = fog[5];
    tweaks.fogB = fog[6];
    tweaks.fogLum = fog[7];
    tweaks.fogStart = fog[8];
    tweaks.fogOpacity = fog[9];
    tweaks.fogInStart = fog[10];
    tweaks.rainAmount = sceneBlockAt(block, kScRainAmount);
    tweaks.rainWet = sceneBlockAt(block, kScRainWet);
    tweaks.rainRipple = sceneBlockAt(block, kScRainRipple);
    tweaks.sunZenith = sceneBlockAt(block, kScSunZenith);
    tweaks.sunAzimuth = sceneBlockAt(block, kScSunAzimuth);
    tweaks.cloudCoverage = sceneBlockAt(block, kScCloudCoverage);
    tweaks.cloudDensity = sceneBlockAt(block, kScCloudDensity);
    tweaks.windSpeed = sceneBlockAt(block, kScWindSpeed);
    tweaks.windYaw = sceneBlockAt(block, kScWindYaw);
    tweaks.temperatureC = sceneBlockAt(block, kScTemperatureC);
    tweaks.dayLength = sceneBlockAt(block, kScDayLength);
    tweaks.timeOfDay = sceneBlockAt(block, kScTimeOfDay);
    tweaks.clockSun = sceneBlockFlag(block, kFlWxClock);
    tweaks.sunKelvin = sceneBlockAt(block, kScSunKelvin);
    tweaks.sunAngle = sceneBlockAt(block, kScSunAngle);
    tweaks.sunSoft = sceneBlockAt(block, kScSunSoft);
    tweaks.sunIndirect = sceneBlockAt(block, kScSunIndirect);
    tweaks.sunEnabled = sceneBlockFlag(block, kFlSun);
    tweaks.sunCast = sceneBlockFlag(block, kFlSunCast);
    tweaks.sunCsm = sceneBlockFlag(block, kFlSunCsm);
    tweaks.tonemapper = sceneBlockFlag(block, kFlTone);
    tweaks.freeze = sceneBlockFlag(block, kFlFreeze) != 0;
    return tweaks;
}

static_assert(kPassRestir == (1u << 20));
static_assert(kPassFsr == (1u << 22));
static_assert(kPassSharc == (1u << 23));

void followHandLamp(SceneFrameImpl& s) {
    s.handOn = false;
}

void sceneFrameSetBlock(SceneFrame* scene, const SceneBlock& block) {
    if (scene == nullptr) {
        return;
    }
    SceneBlock stored = block;
    stored.passMask = sceneBlockPassMask(stored);
    scene->block = stored;
    const SceneTweaks tweaks = sceneBlockTweaks(stored);
    scene->hud = sceneBlockPost(block);
    scene->forceLod = tweaks.forceLod;
    scene->passMask = tweaks.passMask;
    scene->lightProfile = tweaks.lightProfile;
    scene->sscsReach = tweaks.sscsReach;
    scene->sscsThick = tweaks.sscsThick;
    scene->sscsSteps = tweaks.sscsSteps;
    scene->sscsPx = tweaks.sscsPx;
    scene->ssrcBase = tweaks.ssrcBase;
    scene->ssrcThick = tweaks.ssrcThick;
    scene->ssrcCap = tweaks.ssrcCap;
    scene->ssrcSteps = tweaks.ssrcSteps;
    scene->rcRays = tweaks.rcRays;
    scene->rcSpacing = tweaks.rcSpacing;
    scene->rcIntensity = tweaks.rcIntensity;
    scene->rcMax = tweaks.rcMax;
    scene->rcSun = tweaks.rcSun;
    scene->rcBounce = tweaks.rcBounce;
    scene->giGain = tweaks.giGain;
    scene->sharcDenoise = tweaks.sharcDenoise;
    const std::span<const float> restir = sceneBlockSpan(block, kScRestir, kScRestirN);
    scene->restir.knobLocal = static_cast<uint32_t>(std::clamp(restir[0], 0.0f, 16.0f));
    scene->restir.knobSpatial = static_cast<uint32_t>(std::clamp(restir[1], 0.0f, 32.0f));
    scene->restir.knobRadius = std::clamp(restir[2], 1.0f, 64.0f);
    scene->restir.knobHistory = static_cast<uint32_t>(std::clamp(restir[3], 1.0f, 64.0f));
    scene->restir.knobBoil = std::clamp(restir[4], 0.0f, 1.0f);
    scene->restir.knobTemporalDepth = std::clamp(restir[5], 0.001f, 1.0f);
    scene->restir.knobTemporalNormal = std::clamp(restir[6], 0.0f, 1.0f);
    scene->restir.knobTemporalBias = static_cast<uint32_t>(std::clamp(restir[7], 0.0f, 2.0f));
    scene->restir.knobSpatialDepth = std::clamp(restir[8], 0.001f, 1.0f);
    scene->restir.knobSpatialNormal = std::clamp(restir[9], 0.0f, 1.0f);
    scene->restir.knobSpatialBias = static_cast<uint32_t>(std::clamp(restir[10], 0.0f, 3.0f));
    scene->restir.knobSpatialBoost = static_cast<uint32_t>(std::clamp(restir[11], 0.0f, 32.0f));
    scene->restir.knobSunDisk = std::clamp(restir[12], 0.0f, 4.0f);
    scene->restir.knobSunRays = static_cast<uint32_t>(std::clamp(restir[13], 1.0f, 8.0f));
    scene->restir.knobShadowRays = static_cast<uint32_t>(std::clamp(restir[14], 1.0f, 8.0f));
    scene->restir.knobShadowRange = std::clamp(restir[15], 0.0f, 200.0f);
    scene->restir.knobShadowBias = std::clamp(restir[16], 0.02f, 2.0f);
    scene->restir.knobPermTh = std::clamp(restir[17], 0.0f, 1.0f);
    scene->restir.knobTarget = static_cast<uint32_t>(std::clamp(restir[18], 1.0f, 64.0f));
    scene->restir.knobTemporalOn = sceneBlockFlag(block, kFlRestirTemporal);
    scene->restir.knobSpatialOn = sceneBlockFlag(block, kFlRestirSpatial);
    scene->restir.knobBoilOn = sceneBlockFlag(block, kFlRestirBoil);
    scene->restir.knobInitVis = sceneBlockFlag(block, kFlRestirInitVis);
    scene->restir.knobPerm = sceneBlockFlag(block, kFlRestirPerm);
    scene->restir.knobShortcut = sceneBlockFlag(block, kFlRestirShortcut);
    scene->restir.knobMaterial = sceneBlockFlag(block, kFlRestirMaterial);
    scene->restir.knobNaive = sceneBlockFlag(block, kFlRestirNaive);
    scene->restir.knobSunOn = (sceneBlockFlag(block, kFlRestirSun) != 0 && tweaks.sunEnabled != 0 && tweaks.sunCsm == 0) ? 1u : 0u;
    scene->restir.knobSunDisk = std::clamp(tweaks.sunAngle + tweaks.sunSoft, 0.0f, 8.0f);
    scene->fsr.quality = tweaks.fsrQuality > 4u ? 0u : tweaks.fsrQuality;
    scene->fsr.sharpness = std::clamp(tweaks.fsrSharpness, 0.0f, 1.0f);
    scene->bloomThreshold = tweaks.bloomThreshold;
    scene->rainRipple = std::clamp(tweaks.rainRipple, 0.0f, 1.0f);
    if (Weather* climate = scene->weatherEnt.try_get_mut<Weather>()) {
        weatherGoals(*climate, tweaks.cloudCoverage, tweaks.cloudDensity, tweaks.rainAmount, tweaks.rainWet, tweaks.fogDensity, tweaks.sunZenith, tweaks.sunAzimuth, tweaks.windSpeed, tweaks.windYaw, tweaks.temperatureC, tweaks.dayLength, tweaks.timeOfDay, tweaks.clockSun);
        if (tweaks.sunEnabled != 0) {
            climate->flags |= 2u;
        } else {
            climate->flags &= ~2u;
        }
        if (tweaks.sunCast != 0) {
            climate->flags |= 4u;
        } else {
            climate->flags &= ~4u;
        }
        if (tweaks.sunCsm != 0) {
            climate->flags |= 8u;
        } else {
            climate->flags &= ~8u;
        }
        climate->kelvin = tweaks.sunKelvin;
        climate->sunAngle = tweaks.sunAngle;
        climate->softAngle = tweaks.sunSoft;
        climate->indirect = tweaks.sunIndirect;
    } else {
        scene->rainAmount = std::clamp(tweaks.rainAmount, 0.0f, 4.0f);
        scene->rainWet = std::clamp(tweaks.rainWet, 0.0f, 1.0f);
        scene->atmosphere.sunZenith = tweaks.sunZenith;
        scene->atmosphere.sunAzimuth = tweaks.sunAzimuth;
        scene->atmosphere.cloudCoverage = tweaks.cloudCoverage;
        scene->atmosphere.cloudDensity = tweaks.cloudDensity;
    }
    scene->sunEnabled = tweaks.sunEnabled != 0;
    scene->sunCast = tweaks.sunCast != 0;
    scene->sunCsm = tweaks.sunCsm != 0;
    scene->sunKelvin = tweaks.sunKelvin;
    scene->sunIndirect = tweaks.sunIndirect;
    scene->tonemapper = tweaks.tonemapper > 7u ? 0u : tweaks.tonemapper;
    scene->freeze = tweaks.freeze;
}

void sceneFrameCamera(SceneFrame* scene, float fovDeg, float nearZ, float farZ) {
    if (scene == nullptr) {
        return;
    }
    fovDeg = clampRange(fovDeg, 10.0f, 120.0f);
    nearZ = clampRange(nearZ, 0.01f, 10.0f);
    farZ = clampRange(farZ, 100.0f, 100000.0f);
    if (nearZ >= farZ * 0.5f) {
        nearZ = farZ * 0.01f;
    }
    scene->fovYRad = fovDeg * 0.017453292f;
    scene->nearPlane = nearZ;
    scene->farPlane = farZ;
}

void sceneFrameCameraSpeed(SceneFrame* scene, float speed) {
    if (scene == nullptr) {
        return;
    }
    scene->fly.speed = clampRange(speed, 0.05f, 200.0f);
}

void sceneFrameCameraView(SceneFrame* scene, uint8_t view) {
    if (scene == nullptr) {
        return;
    }
    if (view > 6) {
        view = 0;
    }
    if (view == scene->camFacing) {
        return;
    }
    if (view == 0) {
        scene->camFacing = 0;
        scene->camView = scene->camOrtho;
        return;
    }
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = 0.0f;
    flyCamForward(scene->fly, fx, fy, fz);
    constexpr float kDist = 14.0f;
    const float pivot[3] = {
        scene->fly.eye[0] + fx * kDist,
        scene->fly.eye[1] + fy * kDist,
        scene->fly.eye[2] + fz * kDist,
    };
    float yaw = 0.0f;
    float pitch = 0.0f;
    const uint8_t axis = static_cast<uint8_t>(view - 1);
    if (axis == 0) {
        pitch = -1.55f;
    } else if (axis == 1) {
        pitch = 1.55f;
    } else if (axis == 2) {
        yaw = -1.5707963f;
    } else if (axis == 3) {
        yaw = 1.5707963f;
    } else if (axis == 5) {
        yaw = 3.14159265f;
    }
    scene->fly.yaw = yaw;
    scene->fly.pitch = pitch;
    flyCamForward(scene->fly, fx, fy, fz);
    scene->fly.eye[0] = pivot[0] - fx * kDist;
    scene->fly.eye[1] = pivot[1] - fy * kDist;
    scene->fly.eye[2] = pivot[2] - fz * kDist;
    scene->camFacing = view;
    scene->camView = scene->camOrtho;
}

void sceneFrameCameraProject(SceneFrame* scene, uint8_t ortho) {
    if (scene == nullptr) {
        return;
    }
    scene->camOrtho = ortho != 0 ? 1 : 0;
    scene->camView = scene->camOrtho;
}

uint8_t sceneFrameCameraProject(const SceneFrame* scene) {
    return scene != nullptr && scene->camOrtho != 0 ? 1 : 0;
}

void sceneFrameCameraRelease(SceneFrame* scene) {
    if (scene == nullptr || scene->camFacing == 0) {
        return;
    }
    scene->camFacing = 0;
    scene->camView = scene->camOrtho;
}

uint8_t sceneFrameCameraMode(const SceneFrame* scene) {
    return scene != nullptr ? scene->camFacing : 0;
}

float sceneFrameCameraFov(const SceneFrame* scene) {
    return scene != nullptr ? scene->fovYRad * 57.2957795f : 60.0f;
}

float sceneFrameCameraNear(const SceneFrame* scene) {
    return scene != nullptr ? scene->nearPlane : 0.1f;
}

float sceneFrameCameraFar(const SceneFrame* scene) {
    return scene != nullptr ? scene->farPlane : 8000.0f;
}

float sceneFrameCameraSpeed(const SceneFrame* scene) {
    return scene != nullptr ? scene->fly.speed : 4.0f;
}

void sceneFrameWeatherPreset(SceneFrame* scene, uint32_t preset) {
    if (scene == nullptr) {
        return;
    }
    if (Weather* climate = scene->weatherEnt.try_get_mut<Weather>()) {
        weatherPreset(*climate, preset);
    }
}

float sceneFrameWeatherTime(const SceneFrame* scene) {
    if (scene == nullptr) {
        return -1.0f;
    }
    const Weather* climate = scene->weatherEnt.try_get<Weather>();
    return climate != nullptr ? climate->timeOfDay : -1.0f;
}

float sceneFrameWeatherZenith(const SceneFrame* scene) {
    if (scene == nullptr) {
        return 35.0f;
    }
    const Weather* climate = scene->weatherEnt.try_get<Weather>();
    return climate != nullptr ? climate->sunZenith : 35.0f;
}

float sceneFrameWeatherAzimuth(const SceneFrame* scene) {
    if (scene == nullptr) {
        return 30.0f;
    }
    const Weather* climate = scene->weatherEnt.try_get<Weather>();
    return climate != nullptr ? climate->sunAzimuth : 30.0f;
}

void sceneFillUniforms(SceneFrame* scene, Device& device, const Swapchain& swap, const FrameContext& frame, bool fsrLive, SceneUniformSet& out) {
    CameraPose& pose = out.pose;
    pose.eye[0] = scene->fly.eye[0];
    pose.eye[1] = scene->fly.eye[1];
    pose.eye[2] = scene->fly.eye[2];
    const float stepX = scene->fly.eye[0] - scene->camPrevEye[0];
    const float stepY = scene->fly.eye[1] - scene->camPrevEye[1];
    const float stepZ = scene->fly.eye[2] - scene->camPrevEye[2];
    const float step = std::sqrt(stepX * stepX + stepY * stepY + stepZ * stepZ);
    const float stepCap = step > 0.04f ? 0.04f : step;
    if (stepCap > 0.001f) {
        scene->camBob += stepCap * 4.0f;
        if (scene->camBob > 6.2831853f) {
            scene->camBob -= 6.2831853f;
        }
    }
    scene->camPrevEye[0] = scene->fly.eye[0];
    scene->camPrevEye[1] = scene->fly.eye[1];
    scene->camPrevEye[2] = scene->fly.eye[2];
    const float bob = std::sin(scene->camBob) * 0.01f;
    pose.eye[1] += bob;
    flyCamTarget(scene->fly, pose.target);
    pose.target[1] += bob + scene->camShock * 0.18f;
    pose.aspect = static_cast<float>(scene->extent.width) / static_cast<float>(scene->extent.height);
    pose.fovYRad = scene->fovYRad;
    pose.zn = scene->nearPlane > 1.0e-4f ? scene->nearPlane : 0.1f;
    pose.zf = scene->farPlane;
    if (scene->camOrtho != 0) {
        const float dx = pose.target[0] - pose.eye[0];
        const float dy = pose.target[1] - pose.eye[1];
        const float dz = pose.target[2] - pose.eye[2];
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 0.5f) {
            dist = 0.5f;
        }
        const float zoom = scene->camZoom > 0.02f ? scene->camZoom : 0.02f;
        pose.orthoHalfY = dist * std::tan(pose.fovYRad * 0.5f) * zoom;
    }
    const bool taaLive = (static_cast<uint32_t>(scene->hud.gradeFlags) & 8u) != 0u && !fsrLive;
    if (fsrLive || taaLive) {
        fsrJitter(scene->fsr, pose.jitterX, pose.jitterY, scene->extent, swap.extent);
    }
    if (!taaLive) {
        scene->tone.taaReady = 0;
    }
    CameraPose bare = pose;
    bare.jitterX = 0.0f;
    bare.jitterY = 0.0f;
    const CameraCull unjit = cameraCullBuild(bare, nullptr);
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    float unjitVp[16]{};
    float jitInv[16]{};
    std::memcpy(unjitVp, unjit.viewProj.m, sizeof(unjitVp));
    std::memcpy(jitInv, cam.invViewProj.m, sizeof(jitInv));
    sceneActorSetCamera(scene->actors, cam, static_cast<float>(swap.extent.width), static_cast<float>(swap.extent.height));

    const uint32_t flight = frame.flight & 1u;
    const uint32_t strideU = static_cast<uint32_t>(alignUp(scene->visibleCap));
    scene->cube.listStride = strideU;
    const bool occl = scene->occl.compact.handle != VK_NULL_HANDLE && scene->indirect.mapped != nullptr;
    uint32_t nInst = 0;
    if (!occl) {
        nInst = cullLod(*scene, cam);
        scene->visibleCount = nInst;
        if (nInst > 0 && scene->visible.mapped != nullptr) {
            std::memcpy(scene->visible.mapped, scene->visibleCpu.data(), sizeof(uint32_t) * nInst);
        }
    }

    FrameView& gpu = out.gpu;
    FrameSun& lit = out.lit;
    FramePost& knob = out.knob;
    const std::span<const float> fog = sceneBlockSpan(scene->block, kScFog, kScFogN);
    float publishedFog = fog[0];
    std::memcpy(gpu.viewProj, cam.viewProj.m, sizeof(cam.viewProj.m));
    std::memcpy(gpu.invViewProj, cam.invViewProj.m, sizeof(cam.invViewProj.m));
    if (const Weather* climate = scene->weatherEnt.try_get<Weather>()) {
        weatherPublish(*climate, scene->atmosphere, scene->rainAmount, scene->rainWet, publishedFog);
    }
    scene->atmosphere.time = scene->skyTime;
    scene->atmosphere.parity = (scene->frameCursor & 1u) != 0u ? 1.0f : 0.0f;
    if (!scene->sunEnabled) {
        scene->atmosphere.sunIntensity = 0.0f;
    }
    atmosphereApply(lit, scene->atmosphere);
    lit.sunDirIntensity[3] *= scene->hud.sunGain;
    if (!scene->sunEnabled) {
        lit.sunDirIntensity[3] = 0.0f;
    }
    lit.pad[3] = scene->sunIndirect;
    float kelvinR = 1.0f;
    float kelvinG = 1.0f;
    float kelvinB = 1.0f;
    colorFromKelvin(scene->sunKelvin, kelvinR, kelvinG, kelvinB);
    lit.sunColor[0] = scene->hud.sunR * kelvinR;
    lit.sunColor[1] = scene->hud.sunG * kelvinG;
    lit.sunColor[2] = scene->hud.sunB * kelvinB;
    gpu.cameraPos[0] = cam.pos[0];
    gpu.cameraPos[1] = cam.pos[1];
    gpu.cameraPos[2] = cam.pos[2];
    gpu.cameraPos[3] = pose.zn;
    gpu.invExtent[0] = 1.0f / static_cast<float>(scene->extent.width);
    gpu.invExtent[1] = 1.0f / static_cast<float>(scene->extent.height);
    gpu.padInv[0] = static_cast<float>(strideU);
    gpu.viewPad[0] = static_cast<float>(scene->instanceCount);
    gpu.viewPad[1] = static_cast<float>(flight * 2u * strideU);
    gpu.viewPad[2] = scene->skyTime;
    gpu.viewPad[3] = scene->rainAmount;
    const uint64_t roofTlas = scene->radiance.geometry ? static_cast<uint64_t>(scene->radiance.tlasAddress) : 0ull;
    std::memcpy(&gpu.viewTail[0], &roofTlas, sizeof(roofTlas));
    gpu.viewTail[2] = scene->rainWet;
    gpu.viewTail[3] = scene->rainRipple;
    gpu.padInv[1] = 0.0f;
    gpu.extent[0] = scene->extent.width;
    gpu.extent[1] = scene->extent.height;
    gpu.padExt[0] = swap.extent.width;
    gpu.padExt[1] = swap.extent.height;
    scene->shadowSlot = 0;
    const bool linearTarget = swap.format == VK_FORMAT_B8G8R8A8_UNORM
        || swap.format == VK_FORMAT_R8G8B8A8_UNORM
        || swap.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
        || swap.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    const float sliceNear[3] = {0.1f, 12.0f, 48.0f};
    const float sliceFar[3] = {16.0f, 64.0f, 240.0f};
    const float casterPad[3] = {8.0f, 16.0f, 32.0f};
    const float maxHalf[3] = {8.0f, 24.0f, 64.0f};
    const float snapCells[3] = {8.0f, 4.0f, 8.0f};
    float sunCx = scene->fly.eye[0];
    float sunCy = scene->fly.eye[1];
    float sunCz = scene->fly.eye[2];
    float splits[3] = {36.0f, 180.0f, 2500.0f};
    lit.sunSplit[3] = static_cast<float>(flight * 16u);
    gpu.debugMode = scene->debugMode;
    gpu.frameParity = scene->frameCursor & 1u;
    gpu.disableSSR = 1u;
    gpu.disableSSRC = 1u;
    gpu.disableHiZ = scene->showHiz ? 0u : 1u;
    gpu.disableConeCull = scene->showCone ? 0u : 1u;
    gpu.disableLod = scene->showLod ? 0u : 1u;
    gpu.disableFrustum = scene->showFrustum ? 0u : 1u;
    gpu.disableSubpixel = scene->showSubpixel ? 0u : 1u;
    gpu.disableShadow = scene->showShadow ? 0u : 1u;
    const float sunIndirect = scene->sunIndirect;
    std::memcpy(&gpu.debugPad[1], &sunIndirect, sizeof(sunIndirect));
    gpu.disableBackface = scene->showBackface ? 0u : 1u;
    gpu.disableScreen = scene->showScreen ? 0u : 1u;
    gpu.disableBehind = scene->showBehind ? 0u : 1u;
    if (scene->freeze) {
        if (!scene->freezeHeld) {
            std::memcpy(scene->freezeVp, cam.viewProj.m, sizeof(scene->freezeVp));
            scene->freezeHeld = true;
        }
    } else {
        scene->freezeHeld = false;
    }
    std::memcpy(gpu.freezeVp, scene->freezeVp, sizeof(gpu.freezeVp));
    knob.flags.forceLod = scene->forceLod;
    knob.flags.passMask = scene->passMask;
    knob.flags.lightProfile = scene->lightProfile;
    if (scene->freeze) {
        knob.flags.passMask |= 64u;
    } else {
        knob.flags.passMask &= ~64u;
    }
    const std::span<const float> gtao = sceneBlockSpan(scene->block, kScGtao, kScGtaoN);
    knob.gtao.radius = gtao[0];
    knob.gtao.steps = gtao[1];
    knob.gtao.slices = gtao[2];
    knob.gtao.power = gtao[3];
    knob.gtao.falloff = gtao[4];
    knob.gtao.intensity = scene->hud.gtao;
    knob.gtao.pad[0] = gtao[5];
    const std::span<const float> ssr = sceneBlockSpan(scene->block, kScSsr, kScSsrN);
    knob.ssr.cutoff = ssr[0];
    knob.ssr.steps = ssr[1];
    knob.ssr.stride = ssr[2];
    knob.ssr.thick = ssr[3];
    knob.ssr.pad[0] = ssr[5];
    knob.ssr.pad[1] = ssr[4];
    knob.ssr.pad[2] = ssr[6];
    knob.ssr.maxDist = ssr[7];
    knob.ssr.bias = ssr[8];
    knob.shade.sscsReach = scene->sscsReach;
    knob.shade.sscsThick = scene->sscsThick;
    knob.shade.sscsSteps = scene->sscsSteps;
    knob.shade.sscsPx = scene->sscsPx;
    knob.shade.fogDensity = publishedFog;
    knob.shade.fogHeight = fog[1];
    knob.shade.fogScatter = fog[2];
    knob.shade.sscsAmt = scene->hud.contact;
    knob.shade.specOcc = scene->hud.specOcc;
    knob.shade.toksvig = scene->hud.toksvig;
    knob.shade.pom = scene->hud.pom;
    knob.shade.ggx = scene->hud.ggx;
    knob.shade.gtaoAmt = scene->hud.gtao;
    knob.shade.lamp = scene->hud.lamp;
    knob.shade.pad0 = scene->rcIntensity;
    knob.shade.pad1 = scene->giGain;
    knob.ssrc.strength = scene->hud.ssrc;
    knob.ssrc.base = scene->ssrcBase;
    knob.ssrc.thick = scene->ssrcThick;
    knob.ssrc.cap = scene->ssrcCap;
    knob.ssrc.steps = scene->ssrcSteps;
    knob.rc.rays = scene->rcRays;
    knob.rc.spacing = scene->rcSpacing;
    knob.rc.intensity = scene->rcIntensity;
    knob.rc.maxDist = scene->rcMax;
    knob.rc.cap = 8.0f;
    knob.rc.sunScale = scene->rcSun;
    knob.rc.bounce = scene->rcBounce;
    knob.rc.fogFalloff = fog[3];
    knob.rc.fogR = fog[4];
    knob.rc.fogG = fog[5];
    knob.rc.fogB = fog[6];
    knob.rc.fogLum = fog[7];
    knob.rc.fogStart = fog[8];
    knob.rc.fogOpacity = fog[9];
    knob.rc.fogInStart = fog[10];
    knob.rc.fogInSize = fog[2];
    knob.bloom.threshold = scene->bloomThreshold;
    knob.bloom.tonemapper = scene->tonemapper;
    knob.bloom.grain = scene->hud.grain;
    knob.bloom.bloomMix = scene->hud.bloom;
    knob.bloom.vignette = scene->hud.vignette;
    knob.bloom.ca = scene->hud.ca;
    knob.bloom.pad[0] = scene->hud.brightness;
    knob.bloom.pad[1] = linearTarget ? 1.0f : 0.0f;
    knob.bloom.pad[2] = -scene->fsr.jitterX;
    knob.bloom.pad[3] = scene->fsr.jitterY;
    knob.bloom.pad[4] = scene->tone.taaReady != 0u ? 0.12f : 1.0f;
    std::memcpy(static_cast<void*>(&knob.sliders), &scene->hud.exposure, sizeof(PostPack));
    gpu.debugPad[0] = scene->decalCount;
    if (scene->sunEnabled && !scene->sunCast) {
        gpu.debugPad[0] |= 0x80000000u;
    }
    gpu.debugPad[1] = kProbeGrid;
    gpu.debugPad[2] = sectorVisibleMask(scene->sectors, scene->sectorCount, scene->portals, scene->portalCount, &cam.planes[0][0]);
        if (scene->shadowPageBuf.mapped != nullptr) {
        auto* pages = static_cast<uint32_t*>(scene->shadowPageBuf.mapped);
        std::memset(pages, 0, sizeof(uint32_t) * 768u);
        const int cellX = static_cast<int>(std::floor(cam.pos[0] / 8.0f));
        const int cellZ = static_cast<int>(std::floor(cam.pos[2] / 8.0f));
        for (uint32_t c = 0; c < 3; ++c) {
            const int x0 = scene->pageCell[c][0];
            const int z0 = scene->pageCell[c][1];
            const uint32_t crossed = scene->pageReady ? shadowPagesCrossed(x0, z0, cellX, cellZ) : 256u;
            if (crossed > 0u) {
                const int xLo = scene->pageReady ? (x0 < cellX ? x0 : cellX) : cellX;
                const int xHi = scene->pageReady ? (x0 > cellX ? x0 : cellX) : cellX + 15;
                const int zLo = scene->pageReady ? (z0 < cellZ ? z0 : cellZ) : cellZ;
                const int zHi = scene->pageReady ? (z0 > cellZ ? z0 : cellZ) : cellZ + 15;
                for (int z = zLo; z <= zHi; ++z) {
                    for (int x = xLo; x <= xHi; ++x) {
                        const uint32_t px = static_cast<uint32_t>(x) & 15u;
                        const uint32_t pz = static_cast<uint32_t>(z) & 15u;
                        pages[c * 256u + pz * 16u + px] = 1u;
                    }
                }
            }
            scene->pageCell[c][0] = cellX;
            scene->pageCell[c][1] = cellZ;
        }
        scene->pageReady = true;
        gpuBufferFlush(scene->shadowPageBuf, device, 0, sizeof(uint32_t) * 768u);
    }
    if (scene->texPageBuf.mapped != nullptr && scene->texFeedbackBuf.mapped != nullptr) {
        auto* pages = static_cast<uint32_t*>(scene->texPageBuf.mapped);
        auto* feedback = static_cast<uint32_t*>(scene->texFeedbackBuf.mapped);
        if ((scene->passMask & 32768u) != 0u) {
            std::memset(pages, 0, sizeof(uint32_t) * 64u);
            pages[0] = 1u;
            const uint32_t n = feedback[0] < 63u ? feedback[0] : 63u;
            for (uint32_t i = 0; i < n; ++i) {
                pages[feedback[1u + i] & 63u] = 1u;
            }
        } else {
            for (uint32_t i = 0; i < 64u; ++i) {
                pages[i] = 1u;
            }
        }
        feedback[0] = 0u;
    }
    const VkDeviceSize shadowStride = alignUp(sizeof(uint32_t) * scene->visibleCap);
    for (uint32_t c = 0; c < 3; ++c) {
        const SunShadow sun = sunShadowSlice(
            cam, -lit.sunDirIntensity[0], -lit.sunDirIntensity[1], -lit.sunDirIntensity[2],
            sliceNear[c], sliceFar[c], casterPad[c], snapCells[c], maxHalf[c]);
        std::memcpy(lit.sunCascade[c], sun.viewProj.m, sizeof(sun.viewProj.m));
        lit.sunSplit[c] = sun.radius;
        splits[c] = sun.radius;
        if (c == 0) {
            sunCx = cam.pos[0] + cam.forward[0] * 16.0f;
            sunCy = cam.pos[1] + cam.forward[1] * 16.0f;
            sunCz = cam.pos[2] + cam.forward[2] * 16.0f;
        }
        scene->shadowCount[c] = 0;
    }
    int32_t nearId[3] = {-1, -1, -1};
    float nearD[3] = {1.0e30f, 1.0e30f, 1.0e30f};
    for (uint32_t i = 0; i < scene->importedCount; ++i) {
        LightGpuData& lamp = scene->imported[i];
        if (lamp.type != kLightPoint && lamp.type != kLightSpot) {
            lamp.shadowMapIndex = -1;
            continue;
        }
        const float dx = lamp.pos[0] - scene->fly.eye[0];
        const float dy = lamp.pos[1] - scene->fly.eye[1];
        const float dz = lamp.pos[2] - scene->fly.eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        lamp.shadowMapIndex = -1;
        for (int slot = 0; slot < 3; ++slot) {
            if (d2 >= nearD[slot]) {
                continue;
            }
            for (int shift = 2; shift > slot; --shift) {
                nearD[shift] = nearD[shift - 1];
                nearId[shift] = nearId[shift - 1];
            }
            nearD[slot] = d2;
            nearId[slot] = static_cast<int32_t>(i);
            break;
        }
    }
    float pointSig = 0.0f;
    uint32_t nearCount = 0;
    for (int slot = 0; slot < 3; ++slot) {
        if (nearId[slot] < 0) {
            continue;
        }
        scene->imported[static_cast<uint32_t>(nearId[slot])].shadowMapIndex = slot;
        scene->world.lights[nearCount] = scene->imported[static_cast<uint32_t>(nearId[slot])];
        pointSig += scene->world.lights[nearCount].pos[0] + scene->world.lights[nearCount].pos[1] + scene->world.lights[nearCount].pos[2];
        nearCount += 1;
    }
    if (nearCount > 0) {
        scene->world.lightCount = nearCount;
    }
    if (pointSig != scene->pointSig) {
        scene->pointReady = false;
        scene->pointSig = pointSig;
        scene->pointCursor = 0;
    }
    for (uint32_t i = 0; i < scene->world.lightCount && i < 3; ++i) {
        lit.pointPos[i][0] = scene->world.lights[i].pos[0];
        lit.pointPos[i][1] = scene->world.lights[i].pos[1];
        lit.pointPos[i][2] = scene->world.lights[i].pos[2];
        lit.pointPos[i][3] = scene->world.lights[i].radius;
        lit.pointColor[i][0] = scene->world.lights[i].color[0];
        lit.pointColor[i][1] = scene->world.lights[i].color[1];
        lit.pointColor[i][2] = scene->world.lights[i].color[2];
        float cd = scene->world.lights[i].intensity;
        if (cd > 0.0f && cd < 64.0f) {
            cd *= kPhotometric;
        }
        lit.pointColor[i][3] = cd / kPhotometric;
        const LightGpuData& lamp = scene->world.lights[i];
        const float dx = lamp.pos[0] - cam.pos[0];
        const float dy = lamp.pos[1] - cam.pos[1];
        const float dz = lamp.pos[2] - cam.pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float screenPx = lamp.radius / std::max(dist, 0.5f) * static_cast<float>(scene->extent.height);
        if (lamp.type == kLightSpot) {
            const uint32_t face = dominantFace(lamp.direction[0], lamp.direction[1], lamp.direction[2]);
            const float tag = static_cast<float>(face + 1u);
            lit.pad[i] = spotPage(screenPx) == 64u ? -tag : tag;
        } else {
            lit.pad[i] = 0.0f;
        }
        for (uint32_t face = 0; face < 6; ++face) {
            const Mat4 vp = pointShadowFace(lamp, face);
            std::memcpy(lit.pointFace[i][face], vp.m, sizeof(vp.m));
        }
        if (!scene->pointReady && i == scene->pointCursor) {
            scene->pointCount[i] = cullPoint(*scene, scene->world.lights[i].pos[0], scene->world.lights[i].pos[1], scene->world.lights[i].pos[2], scene->world.lights[i].radius, scene->visibleCpu.data());
            scene->pointCursor = i + 1u;
            if (scene->pointCount[i] > 0 && scene->pointVisible.mapped != nullptr) {
                std::memcpy(static_cast<uint8_t*>(scene->pointVisible.mapped) + shadowStride * i, scene->visibleCpu.data(), sizeof(uint32_t) * scene->pointCount[i]);
            }
        }
    }
    LightGpuData probe{};
    probe.type = kLightPoint;
    probe.radius = scene->shadowExtent > 1.0f ? scene->shadowExtent : 200.0f;
    lit.cubeMin[0] = scene->boxMin[0];
    lit.cubeMin[1] = scene->boxMin[1];
    lit.cubeMin[2] = scene->boxMin[2];
    lit.cubeMax[0] = scene->boxMax[0];
    lit.cubeMax[1] = scene->boxMax[1];
    lit.cubeMax[2] = scene->boxMax[2];
    const float cubeDx = scene->fly.eye[0] - scene->cubeEye[0];
    const float cubeDy = scene->fly.eye[1] - scene->cubeEye[1];
    const float cubeDz = scene->fly.eye[2] - scene->cubeEye[2];
    if (scene->cubeReady) {
        const bool fresh = scene->cubeEye[0] == 0.0f && scene->cubeEye[1] == 0.0f && scene->cubeEye[2] == 0.0f;
        if (fresh || cubeDx * cubeDx + cubeDy * cubeDy + cubeDz * cubeDz > 1600.0f) {
            scene->cubeEye[0] = scene->fly.eye[0];
            scene->cubeEye[1] = scene->fly.eye[1];
            scene->cubeEye[2] = scene->fly.eye[2];
            if (!fresh) {
                scene->cubeReady = false;
                scene->cubeFace = 0;
            }
        }
    }
    if (!scene->cubeReady && scene->cubeFace == 0u) {
        scene->cubeEye[0] = scene->fly.eye[0];
        scene->cubeEye[1] = scene->fly.eye[1];
        scene->cubeEye[2] = scene->fly.eye[2];
    }
    probe.pos[0] = scene->cubeEye[0];
    probe.pos[1] = scene->cubeEye[1];
    probe.pos[2] = scene->cubeEye[2];
    for (uint32_t face = 0; face < 6; ++face) {
        const Mat4 vp = pointShadowFace(probe, face);
        std::memcpy(lit.cubeFace[face], vp.m, sizeof(vp.m));
        if (!scene->cubeReady && face == scene->cubeFace) {
            const float dir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const float up[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
            const Mat4 view = pointShadowLook(
                probe.pos[0], probe.pos[1], probe.pos[2],
                dir[face][0], dir[face][1], dir[face][2],
                up[face][0], up[face][1], up[face][2]);
            scene->cubeCount[face] = cullFace(*scene, view, scene->visibleCpu.data());
            if (scene->cubeCount[face] > 0 && scene->cubeVisible.mapped != nullptr) {
                std::memcpy(static_cast<uint8_t*>(scene->cubeVisible.mapped) + shadowStride * face, scene->visibleCpu.data(), sizeof(uint32_t) * scene->cubeCount[face]);
            }
        }
    }
    uint32_t sunKeep[3]{};
    if (scene->shadowIndirect.mapped != nullptr) {
        gpuBufferInvalidate(scene->shadowIndirect, device, 0, scene->shadowIndirect.size);
        const auto* prevSun = static_cast<const uint32_t*>(scene->shadowIndirect.mapped) + flight * 16u;
        sunKeep[0] = prevSun[0];
        sunKeep[1] = prevSun[4];
        sunKeep[2] = prevSun[8];
        if (sunKeep[0] == 0u && sunKeep[1] == 0u && sunKeep[2] == 0u) {
            scene->shadowStampValid = 0;
        }
    }
    const LightGpuData* lightSrc = scene->importedCount > 0 ? scene->imported : scene->world.lights;
    const uint32_t lightCount = uploadLights(*scene, flight);
    gpu.padInv[1] = static_cast<float>(lightCount);
    std::memcpy(static_cast<uint8_t*>(scene->frameBuf.mapped) + sizeof(FrameView) * flight, &gpu, sizeof(gpu));
    std::memcpy(static_cast<uint8_t*>(scene->sunBuf.mapped) + sizeof(FrameSun) * flight, &lit, sizeof(lit));
    std::memcpy(static_cast<uint8_t*>(scene->postBuf.mapped) + sizeof(FramePost) * flight, &knob, sizeof(knob));
    GradeBlock grade{};
    const float bleed = scene->hud.mix;
    grade.mixR[0] = 1.0f;
    grade.mixR[1] = bleed;
    grade.mixR[2] = bleed;
    grade.mixG[0] = bleed;
    grade.mixG[1] = 1.0f;
    grade.mixG[2] = bleed;
    grade.mixB[0] = bleed;
    grade.mixB[1] = bleed;
    grade.mixB[2] = 1.0f;
    grade.shadows[0] = grade.shadows[1] = grade.shadows[2] = scene->hud.shadows;
    grade.midtones[0] = grade.midtones[1] = grade.midtones[2] = scene->hud.midtones;
    grade.highlights[0] = grade.highlights[1] = grade.highlights[2] = scene->hud.highlights;
    grade.slope[0] = grade.slope[1] = grade.slope[2] = scene->hud.slope;
    grade.offset[0] = grade.offset[1] = grade.offset[2] = scene->hud.cdlOffset;
    grade.power[0] = grade.power[1] = grade.power[2] = scene->hud.power;
    grade.ranges[0] = 0.0f;
    grade.ranges[1] = 0.333f;
    grade.ranges[2] = 0.55f;
    grade.ranges[3] = 1.0f;
    grade.generic[0] = scene->hud.genContrast;
    grade.generic[1] = scene->hud.midIn;
    grade.generic[2] = scene->hud.midOut;
    grade.generic[3] = scene->hud.hdrMax;
    grade.curves[0] = 1.0f;
    grade.curves[1] = 1.0f;
    grade.curves[2] = 1.0f;
    grade.curves[3] = scene->hud.vibrance;
    std::memcpy(grade.prev0, scene->prevVp + 0, sizeof(float) * 4);
    std::memcpy(grade.prev1, scene->prevVp + 4, sizeof(float) * 4);
    std::memcpy(grade.prev2, scene->prevVp + 8, sizeof(float) * 4);
    std::memcpy(grade.prev3, scene->prevVp + 12, sizeof(float) * 4);
    std::memcpy(static_cast<uint8_t*>(scene->postBuf.mapped) + sizeof(FramePost) * 2 + sizeof(GradeBlock) * flight, &grade, sizeof(grade));
    std::memcpy(scene->prevVp, cam.viewProj.m, sizeof(scene->prevVp));
    if (scene->atmBuf.mapped != nullptr) {
        std::memcpy(static_cast<uint8_t*>(scene->atmBuf.mapped) + sizeof(AtmosphereParams) * flight, &scene->atmosphere, sizeof(AtmosphereParams));
    }
    out.taaLive = taaLive;
    out.flight = flight;
    out.strideU = strideU;
    out.lightSrc = lightSrc;
    out.lightCount = lightCount;
    std::memcpy(out.unjitVp, unjitVp, sizeof(unjitVp));
    std::memcpy(out.jitInv, jitInv, sizeof(jitInv));
    std::memcpy(out.sunKeep, sunKeep, sizeof(sunKeep));
    std::memcpy(out.splits, splits, sizeof(splits));
    out.sunCentre[0] = sunCx;
    out.sunCentre[1] = sunCy;
    out.sunCentre[2] = sunCz;
}

} // namespace burnhope
