#pragma once

#include "app/Engine.hpp"

#include <cstring>
#include <type_traits>

namespace burnhope {

// One pack of the four live records into the window block. Callers do not assign scalars themselves.
inline void sceneBlockFill(SceneBlock& block, const HudPost& post, const SceneTweaks& tweaks, const HudFlags& flags, const HudActor& actor) {
    block.flags[kFlHiz] = flags.hiz;
    block.flags[kFlCone] = flags.cone;
    block.flags[kFlLod] = flags.lod;
    block.flags[kFlFrust] = flags.frust;
    block.flags[kFlSmall] = flags.small;
    block.flags[kFlBackf] = flags.backf;
    block.flags[kFlScrn] = flags.scrn;
    block.flags[kFlBehind] = flags.behind;
    block.flags[kFlShadowCull] = flags.shadowCull;
    block.flags[kFlProbes] = flags.probes;
    block.flags[kFlCluster] = flags.cluster;
    block.flags[kFlPoints] = flags.points;
    block.flags[kFlSpots] = flags.spots;
    block.flags[kFlArea] = flags.area;
    block.flags[kFlSky] = flags.sky;
    block.flags[kFlCloud] = flags.clouden;
    block.flags[kFlFog] = flags.fogen;
    block.flags[kFlBloom] = flags.bloomen;
    block.flags[kFlContact] = flags.contacten;
    block.flags[kFlVt] = flags.vt;
    block.flags[kFlLumScale] = flags.lumScale;
    block.flags[kFlGamut] = flags.gamut;
    block.flags[kFlSun] = flags.sun;
    block.flags[kFlSunCast] = flags.sunCast;
    block.flags[kFlWxClock] = flags.wxClock;
    block.flags[kFlTone] = flags.tone;
    block.flags[kFlAa] = flags.aa;
    block.flags[kFlFsrQ] = flags.fsrQ;
    block.flags[kFlGi] = flags.giMode;
    block.flags[kFlDirect] = flags.directMode;
    block.flags[kFlReflect] = flags.reflectMode;
    block.flags[kFlAo] = flags.aoMode;
    block.flags[kFlSunShadow] = flags.sunShadow;
    block.flags[kFlView] = flags.viewMode;
    block.flags[kFlSharcDenoise] = tweaks.sharcDenoise ? 1u : 0u;
    block.flags[kFlRestirTemporal] = tweaks.restirTemporal ? 1u : 0u;
    block.flags[kFlRestirSpatial] = tweaks.restirSpatialOn ? 1u : 0u;
    block.flags[kFlRestirBoil] = tweaks.restirBoilOn ? 1u : 0u;
    block.flags[kFlRestirInitVis] = tweaks.restirInitVis ? 1u : 0u;
    block.flags[kFlRestirPerm] = tweaks.restirPerm ? 1u : 0u;
    block.flags[kFlRestirShortcut] = tweaks.restirShortcut ? 1u : 0u;
    block.flags[kFlRestirMaterial] = tweaks.restirMaterial ? 1u : 0u;
    block.flags[kFlRestirNaive] = tweaks.restirNaive ? 1u : 0u;
    block.flags[kFlRestirSun] = tweaks.restirSunOn ? 1u : 0u;
    block.flags[kFlFreeze] = tweaks.freeze ? 1u : 0u;
    block.flags[kFlActorStatic] = actor.isStatic;
    block.flags[kFlSun] = static_cast<uint8_t>(tweaks.sunEnabled);
    block.flags[kFlSunCast] = static_cast<uint8_t>(tweaks.sunCast);
    block.flags[kFlSunCsm] = static_cast<uint8_t>(tweaks.sunCsm);
    block.flags[kFlWxClock] = static_cast<uint8_t>(tweaks.clockSun);
    block.flags[kFlTone] = static_cast<uint8_t>(tweaks.tonemapper);
    block.flags[kFlFsrQ] = static_cast<uint8_t>(tweaks.fsrQuality);
    block.flags[kFlGi] = static_cast<uint8_t>(tweaks.lightProfile);

    static_assert(std::is_standard_layout_v<HudPost>);
    static_assert(sizeof(HudPost) == kScPostN * sizeof(float));
    static_assert(offsetof(HudPost, grain) == 0);
    static_assert(offsetof(HudPost, gtao) == 11 * sizeof(float));
    static_assert(offsetof(HudPost, vibrance) == 38 * sizeof(float));
    std::memcpy(block.scalars + kScPost, &post, sizeof(HudPost));

    block.scalars[kScGtaoRadius] = tweaks.gtaoRadius;
    block.scalars[kScGtaoSteps] = tweaks.gtaoSteps;
    block.scalars[kScGtaoSlices] = tweaks.gtaoSlices;
    block.scalars[kScGtaoPower] = tweaks.gtaoPower;
    block.scalars[kScGtaoFalloff] = tweaks.gtaoFalloff;
    block.scalars[kScGtaoDenoise] = tweaks.gtaoDenoise;
    block.scalars[kScSsrCutoff] = tweaks.ssrCutoff;
    block.scalars[kScSsrSteps] = tweaks.ssrSteps;
    block.scalars[kScSsrStride] = tweaks.ssrStride;
    block.scalars[kScSsrThick] = tweaks.ssrThick;
    block.scalars[kScSsrMetal] = tweaks.ssrMetal;
    block.scalars[kScSsrRt] = tweaks.ssrRt;
    block.scalars[kScSsrEdge] = tweaks.ssrEdge;
    block.scalars[kScSsrMax] = tweaks.ssrMax;
    block.scalars[kScSsrBias] = tweaks.ssrBias;
    block.scalars[kScSscsReach] = tweaks.sscsReach;
    block.scalars[kScSscsThick] = tweaks.sscsThick;
    block.scalars[kScSscsSteps] = tweaks.sscsSteps;
    block.scalars[kScSscsPx] = tweaks.sscsPx;
    block.scalars[kScSsrcBase] = tweaks.ssrcBase;
    block.scalars[kScSsrcThick] = tweaks.ssrcThick;
    block.scalars[kScSsrcCap] = tweaks.ssrcCap;
    block.scalars[kScSsrcSteps] = tweaks.ssrcSteps;
    block.scalars[kScRcRays] = tweaks.rcRays;
    block.scalars[kScRcSpacing] = tweaks.rcSpacing;
    block.scalars[kScRcIntensity] = tweaks.rcIntensity;
    block.scalars[kScRcMax] = tweaks.rcMax;
    block.scalars[kScRcSun] = tweaks.rcSun;
    block.scalars[kScRcBounce] = tweaks.rcBounce;
    block.scalars[kScGiGain] = tweaks.giGain;
    block.scalars[kScRestirLocal] = tweaks.restirLocal;
    block.scalars[kScRestirSpatial] = tweaks.restirSpatial;
    block.scalars[kScRestirRadius] = tweaks.restirRadius;
    block.scalars[kScRestirHistory] = tweaks.restirHistory;
    block.scalars[kScRestirBoil] = tweaks.restirBoil;
    block.scalars[kScRestirTemporalDepth] = tweaks.restirTemporalDepth;
    block.scalars[kScRestirTemporalNormal] = tweaks.restirTemporalNormal;
    block.scalars[kScRestirTemporalBias] = tweaks.restirTemporalBias;
    block.scalars[kScRestirSpatialDepth] = tweaks.restirSpatialDepth;
    block.scalars[kScRestirSpatialNormal] = tweaks.restirSpatialNormal;
    block.scalars[kScRestirSpatialBias] = tweaks.restirSpatialBias;
    block.scalars[kScRestirSpatialBoost] = tweaks.restirSpatialBoost;
    block.scalars[kScRestirSunDisk] = tweaks.restirSunDisk;
    block.scalars[kScRestirSunRays] = tweaks.restirSunRays;
    block.scalars[kScRestirShadowRays] = tweaks.restirShadowRays;
    block.scalars[kScRestirShadowRange] = tweaks.restirShadowRange;
    block.scalars[kScRestirShadowBias] = tweaks.restirShadowBias;
    block.scalars[kScRestirPermTh] = tweaks.restirPermTh;
    block.scalars[kScRestirTarget] = tweaks.restirTarget;
    block.scalars[kScFsrSharpness] = tweaks.fsrSharpness;
    block.scalars[kScBloomThreshold] = tweaks.bloomThreshold;
    block.scalars[kScFogDensity] = tweaks.fogDensity;
    block.scalars[kScFogHeight] = tweaks.fogHeight;
    block.scalars[kScFogScatter] = tweaks.fogScatter;
    block.scalars[kScFogFalloff] = tweaks.fogFalloff;
    block.scalars[kScFogR] = tweaks.fogR;
    block.scalars[kScFogG] = tweaks.fogG;
    block.scalars[kScFogB] = tweaks.fogB;
    block.scalars[kScFogLum] = tweaks.fogLum;
    block.scalars[kScFogStart] = tweaks.fogStart;
    block.scalars[kScFogOpacity] = tweaks.fogOpacity;
    block.scalars[kScFogInStart] = tweaks.fogInStart;
    block.scalars[kScRainAmount] = tweaks.rainAmount;
    block.scalars[kScRainWet] = tweaks.rainWet;
    block.scalars[kScRainRipple] = tweaks.rainRipple;
    block.scalars[kScSunZenith] = tweaks.sunZenith;
    block.scalars[kScSunAzimuth] = tweaks.sunAzimuth;
    block.scalars[kScCloudCoverage] = tweaks.cloudCoverage;
    block.scalars[kScCloudDensity] = tweaks.cloudDensity;
    block.scalars[kScWindSpeed] = tweaks.windSpeed;
    block.scalars[kScWindYaw] = tweaks.windYaw;
    block.scalars[kScTemperatureC] = tweaks.temperatureC;
    block.scalars[kScDayLength] = tweaks.dayLength;
    block.scalars[kScTimeOfDay] = tweaks.timeOfDay;
    block.scalars[kScSunKelvin] = tweaks.sunKelvin;
    block.scalars[kScSunAngle] = tweaks.sunAngle;
    block.scalars[kScSunSoft] = tweaks.sunSoft;
    block.scalars[kScSunIndirect] = tweaks.sunIndirect;
    block.scalars[kScForceLod] = static_cast<float>(tweaks.forceLod);

    static_assert(std::is_standard_layout_v<HudActor>);
    static_assert(offsetof(HudActor, color) == 0);
    static_assert(offsetof(HudActor, pivot) == 15 * sizeof(float));
    static_assert(offsetof(HudActor, isStatic) == kScActorN * sizeof(float));
    std::memcpy(block.scalars + kScActor, &actor, kScActorN * sizeof(float));
}

inline void sceneBlockPutPost(HudPost& post, uint16_t index, float value) {
    switch (index) {
    case kScGrain: post.grain = value; break;
    case kScBrightness: post.brightness = value; break;
    case kScBloom: post.bloom = value; break;
    case kScVignette: post.vignette = value; break;
    case kScCa: post.ca = value; break;
    case kScContact: post.contact = value; break;
    case kScSpecOcc: post.specOcc = value; break;
    case kScToksvig: post.toksvig = value; break;
    case kScPom: post.pom = value; break;
    case kScGgx: post.ggx = value; break;
    case kScSsrcAmt: post.ssrc = value; break;
    case kScGtaoAmt: post.gtao = value; break;
    case kScLamp: post.lamp = value; break;
    case kScSunGain: post.sunGain = value; break;
    case kScSunR: post.sunR = value; break;
    case kScSunG: post.sunG = value; break;
    case kScSunB: post.sunB = value; break;
    case kScExposure: post.exposure = value; break;
    case kScTemperature: post.temperature = value; break;
    case kScTint: post.tint = value; break;
    case kScContrast: post.contrast = value; break;
    case kScSaturation: post.saturation = value; break;
    case kScNight: post.night = value; break;
    case kScShadows: post.shadows = value; break;
    case kScMidtones: post.midtones = value; break;
    case kScHighlights: post.highlights = value; break;
    case kScSlope: post.slope = value; break;
    case kScCdlOffset: post.cdlOffset = value; break;
    case kScPower: post.power = value; break;
    case kScDofFocus: post.dofFocus = value; break;
    case kScDofAmount: post.dofAmount = value; break;
    case kScFlare: post.flare = value; break;
    case kScGradeFlags: post.gradeFlags = value; break;
    case kScMix: post.mix = value; break;
    case kScGenContrast: post.genContrast = value; break;
    case kScMidIn: post.midIn = value; break;
    case kScMidOut: post.midOut = value; break;
    case kScHdrMax: post.hdrMax = value; break;
    case kScVibrance: post.vibrance = value; break;
    default: break;
    }
}

inline void sceneBlockPutActor(HudActor& actor, uint16_t index, float value) {
    const uint16_t slot = static_cast<uint16_t>(index - kScColor0);
    if (slot < 3u) {
        actor.color[slot] = value;
    } else if (slot == 3u) {
        actor.rough = value;
    } else if (slot == 4u) {
        actor.metal = value;
    } else if (slot == 5u) {
        actor.emissive = value;
    } else if (slot < 9u) {
        actor.pos[slot - 6u] = value;
    } else if (slot < 12u) {
        actor.euler[slot - 9u] = value;
    } else if (slot < 15u) {
        actor.scale[slot - 12u] = value;
    } else if (slot < 18u) {
        actor.pivot[slot - 15u] = value;
    }
}

inline void sceneBlockPut(HudContext& hud, uint16_t index, float value) {
    if (index < kScPostN) {
        sceneBlockPutPost(hud.post, index, value);
        return;
    }
    if (index >= kScColor0 && index <= kScPivot2) {
        sceneBlockPutActor(hud.actor, index, value);
        return;
    }
    SceneTweaks& tweaks = hud.tweaks;
    switch (index) {
    case kScGtaoRadius: tweaks.gtaoRadius = value; break;
    case kScGtaoSteps: tweaks.gtaoSteps = value; break;
    case kScGtaoSlices: tweaks.gtaoSlices = value; break;
    case kScGtaoPower: tweaks.gtaoPower = value; break;
    case kScGtaoFalloff: tweaks.gtaoFalloff = value; break;
    case kScGtaoDenoise: tweaks.gtaoDenoise = value; break;
    case kScSsrCutoff: tweaks.ssrCutoff = value; break;
    case kScSsrSteps: tweaks.ssrSteps = value; break;
    case kScSsrStride: tweaks.ssrStride = value; break;
    case kScSsrThick: tweaks.ssrThick = value; break;
    case kScSsrMetal: tweaks.ssrMetal = value; break;
    case kScSsrRt: tweaks.ssrRt = value; break;
    case kScSsrEdge: tweaks.ssrEdge = value; break;
    case kScSsrMax: tweaks.ssrMax = value; break;
    case kScSsrBias: tweaks.ssrBias = value; break;
    case kScSscsReach: tweaks.sscsReach = value; break;
    case kScSscsThick: tweaks.sscsThick = value; break;
    case kScSscsSteps: tweaks.sscsSteps = value; break;
    case kScSscsPx: tweaks.sscsPx = value; break;
    case kScSsrcBase: tweaks.ssrcBase = value; break;
    case kScSsrcThick: tweaks.ssrcThick = value; break;
    case kScSsrcCap: tweaks.ssrcCap = value; break;
    case kScSsrcSteps: tweaks.ssrcSteps = value; break;
    case kScRcRays: tweaks.rcRays = value; break;
    case kScRcSpacing: tweaks.rcSpacing = value; break;
    case kScRcIntensity: tweaks.rcIntensity = value; break;
    case kScRcMax: tweaks.rcMax = value; break;
    case kScRcSun: tweaks.rcSun = value; break;
    case kScRcBounce: tweaks.rcBounce = value; break;
    case kScGiGain: tweaks.giGain = value; break;
    case kScRestirLocal: tweaks.restirLocal = value; break;
    case kScRestirSpatial: tweaks.restirSpatial = value; break;
    case kScRestirRadius: tweaks.restirRadius = value; break;
    case kScRestirHistory: tweaks.restirHistory = value; break;
    case kScRestirBoil: tweaks.restirBoil = value; break;
    case kScRestirTemporalDepth: tweaks.restirTemporalDepth = value; break;
    case kScRestirTemporalNormal: tweaks.restirTemporalNormal = value; break;
    case kScRestirTemporalBias: tweaks.restirTemporalBias = value; break;
    case kScRestirSpatialDepth: tweaks.restirSpatialDepth = value; break;
    case kScRestirSpatialNormal: tweaks.restirSpatialNormal = value; break;
    case kScRestirSpatialBias: tweaks.restirSpatialBias = value; break;
    case kScRestirSpatialBoost: tweaks.restirSpatialBoost = value; break;
    case kScRestirSunDisk: tweaks.restirSunDisk = value; break;
    case kScRestirSunRays: tweaks.restirSunRays = value; break;
    case kScRestirShadowRays: tweaks.restirShadowRays = value; break;
    case kScRestirShadowRange: tweaks.restirShadowRange = value; break;
    case kScRestirShadowBias: tweaks.restirShadowBias = value; break;
    case kScRestirPermTh: tweaks.restirPermTh = value; break;
    case kScRestirTarget: tweaks.restirTarget = value; break;
    case kScFsrSharpness: tweaks.fsrSharpness = value; break;
    case kScBloomThreshold: tweaks.bloomThreshold = value; break;
    case kScFogDensity: tweaks.fogDensity = value; break;
    case kScFogHeight: tweaks.fogHeight = value; break;
    case kScFogScatter: tweaks.fogScatter = value; break;
    case kScFogFalloff: tweaks.fogFalloff = value; break;
    case kScFogR: tweaks.fogR = value; break;
    case kScFogG: tweaks.fogG = value; break;
    case kScFogB: tweaks.fogB = value; break;
    case kScFogLum: tweaks.fogLum = value; break;
    case kScFogStart: tweaks.fogStart = value; break;
    case kScFogOpacity: tweaks.fogOpacity = value; break;
    case kScFogInStart: tweaks.fogInStart = value; break;
    case kScRainAmount: tweaks.rainAmount = value; break;
    case kScRainWet: tweaks.rainWet = value; break;
    case kScRainRipple: tweaks.rainRipple = value; break;
    case kScSunZenith: tweaks.sunZenith = value; break;
    case kScSunAzimuth: tweaks.sunAzimuth = value; break;
    case kScCloudCoverage: tweaks.cloudCoverage = value; break;
    case kScCloudDensity: tweaks.cloudDensity = value; break;
    case kScWindSpeed: tweaks.windSpeed = value; break;
    case kScWindYaw: tweaks.windYaw = value; break;
    case kScTemperatureC: tweaks.temperatureC = value; break;
    case kScDayLength: tweaks.dayLength = value; break;
    case kScTimeOfDay: tweaks.timeOfDay = value; break;
    case kScSunKelvin: tweaks.sunKelvin = value; break;
    case kScSunAngle: tweaks.sunAngle = value; break;
    case kScSunSoft: tweaks.sunSoft = value; break;
    case kScSunIndirect: tweaks.sunIndirect = value; break;
    case kScForceLod: hud.flags.forceLod = value; break;
    case kScMatBase0: hud.matDoc.base[0] = value; break;
    case kScMatBase1: hud.matDoc.base[1] = value; break;
    case kScMatBase2: hud.matDoc.base[2] = value; break;
    case kScMatRough: hud.matDoc.roughness = value; break;
    case kScMatMetal: hud.matDoc.metallic = value; break;
    case kScMatEmissive: hud.matDoc.emissive = value; break;
    case kScMatAlbedoGain: hud.matDoc.albedoGain = value; break;
    case kScMatNormalGain: hud.matDoc.normalGain = value; break;
    case kScMatOrmGain: hud.matDoc.ormGain = value; break;
    case kScMatEmissiveGain: hud.matDoc.emissiveGain = value; hud.matDoc.emissive = value; break;
    case kScMatReflect: hud.matDoc.reflectance = value; break;
    case kScMatAo: hud.matDoc.aoStrength = value; break;
    case kScMatNormal: hud.matDoc.normalScale = value; break;
    case kScMatCoat: hud.matDoc.clearcoat = value; break;
    case kScMatCoatRough: hud.matDoc.clearcoatRoughness = value; break;
    case kScMatGrid: hud.matDoc.worldGridSize = value; break;
    case kScMatSharp: hud.matDoc.triplanarSharpness = value; break;
    case kScMatUvX: hud.matDoc.uvTiling[0] = value; break;
    case kScMatUvY: hud.matDoc.uvTiling[1] = value; break;
    case kScMatUvRot: {
        const float rad = value * 0.01745329252f;
        const float delta = rad - hud.matDoc.uvRotation;
        if (delta > 0.0001f || delta < -0.0001f) {
            hud.matDoc.uvRotation = rad;
        }
        break;
    }
    case kScMatCutoff: hud.matDoc.alphaCutoff = value; break;
    case kScMatEm0: hud.matDoc.emissiveColor[0] = value; break;
    case kScMatEm1: hud.matDoc.emissiveColor[1] = value; break;
    case kScMatEm2: hud.matDoc.emissiveColor[2] = value; break;
    case kScMatHeight: hud.matDoc.heightScale = value; break;
    case kScMatOpacity: hud.matDoc.baseA = value; break;
    case kScMatUvOffX: hud.matDoc.uvOffset[0] = value; break;
    case kScMatUvOffY: hud.matDoc.uvOffset[1] = value; break;
    case kScLiveBase0: hud.live.base[0] = value; break;
    case kScLiveBase1: hud.live.base[1] = value; break;
    case kScLiveBase2: hud.live.base[2] = value; break;
    case kScLiveEm0: hud.live.emissiveColor[0] = value; break;
    case kScLiveEm1: hud.live.emissiveColor[1] = value; break;
    case kScLiveEm2: hud.live.emissiveColor[2] = value; break;
    case kScLiveRough: hud.live.roughness = value; break;
    case kScLiveMetal: hud.live.metallic = value; break;
    case kScLiveReflect: hud.live.reflectance = value; break;
    case kScLiveAo: hud.live.aoStrength = value; break;
    case kScLiveNormal: hud.live.normalScale = value; break;
    case kScLiveEmGain: hud.live.emissiveGain = value; break;
    case kScLiveCoat: hud.live.clearcoat = value; break;
    case kScLiveCoatRough: hud.live.clearcoatRoughness = value; break;
    case kScLiveAlpha: hud.live.alphaCutoff = value; break;
    case kScLiveGrid: hud.live.worldGridSize = value; break;
    case kScLiveSharp: hud.live.triplanarSharpness = value; break;
    case kScLiveOff0: hud.live.worldOffset[0] = value; break;
    case kScLiveOff1: hud.live.worldOffset[1] = value; break;
    case kScLiveOff2: hud.live.worldOffset[2] = value; break;
    case kScLiveUvX: hud.live.uvTiling[0] = value; break;
    case kScLiveUvY: hud.live.uvTiling[1] = value; break;
    case kScLiveUvOffX: hud.live.uvOffset[0] = value; break;
    case kScLiveUvOffY: hud.live.uvOffset[1] = value; break;
    case kScUvDeg: hud.uvDeg = value; break;
    default: break;
    }
}

inline void sceneBlockApply(HudContext& hud) {
    for (const UiBind& row : hud.bind.rows) {
        if (row.kind == kBindFlag) {
            if (row.index >= 64u) {
                continue;
            }
            const uint8_t value = hud.block.flags[row.index];
            switch (row.index) {
            case kFlHiz: hud.flags.hiz = value; break;
            case kFlCone: hud.flags.cone = value; break;
            case kFlLod: hud.flags.lod = value; break;
            case kFlFrust: hud.flags.frust = value; break;
            case kFlSmall: hud.flags.small = value; break;
            case kFlBackf: hud.flags.backf = value; break;
            case kFlScrn: hud.flags.scrn = value; break;
            case kFlBehind: hud.flags.behind = value; break;
            case kFlShadowCull: hud.flags.shadowCull = value; break;
            case kFlProbes: hud.flags.probes = value; break;
            case kFlCluster: hud.flags.cluster = value; break;
            case kFlPoints: hud.flags.points = value; break;
            case kFlSpots: hud.flags.spots = value; break;
            case kFlArea: hud.flags.area = value; break;
            case kFlSky: hud.flags.sky = value; break;
            case kFlCloud: hud.flags.clouden = value; break;
            case kFlFog: hud.flags.fogen = value; break;
            case kFlBloom: hud.flags.bloomen = value; break;
            case kFlContact: hud.flags.contacten = value; break;
            case kFlVt: hud.flags.vt = value; break;
            case kFlLumScale: hud.flags.lumScale = value; break;
            case kFlGamut: hud.flags.gamut = value; break;
            case kFlSun: hud.flags.sun = value; break;
            case kFlSunCast: hud.flags.sunCast = value; break;
            case kFlWxClock: hud.flags.wxClock = value; break;
            case kFlTone: hud.flags.tone = value; break;
            case kFlAa: hud.flags.aa = value; break;
            case kFlFsrQ: hud.flags.fsrQ = value; break;
            case kFlGi: hud.flags.giMode = value; break;
            case kFlDirect: hud.flags.directMode = value; break;
            case kFlReflect: hud.flags.reflectMode = value; break;
            case kFlAo: hud.flags.aoMode = value; break;
            case kFlSunShadow: hud.flags.sunShadow = value; break;
            case kFlView: hud.flags.viewMode = value; break;
            case kFlSharcDenoise: hud.tweaks.sharcDenoise = value != 0; break;
            case kFlRestirTemporal: hud.tweaks.restirTemporal = value != 0; break;
            case kFlRestirSpatial: hud.tweaks.restirSpatialOn = value != 0; break;
            case kFlRestirBoil: hud.tweaks.restirBoilOn = value != 0; break;
            case kFlRestirInitVis: hud.tweaks.restirInitVis = value != 0; break;
            case kFlRestirPerm: hud.tweaks.restirPerm = value != 0; break;
            case kFlRestirShortcut: hud.tweaks.restirShortcut = value != 0; break;
            case kFlRestirMaterial: hud.tweaks.restirMaterial = value != 0; break;
            case kFlRestirNaive: hud.tweaks.restirNaive = value != 0; break;
            case kFlRestirSun: hud.tweaks.restirSunOn = value != 0; break;
            case kFlFreeze: hud.tweaks.freeze = value != 0; break;
            case kFlActorStatic: hud.actor.isStatic = value; break;
            case kFlMatWorld: hud.matDoc.worldAligned = value; break;
            case kFlMatDouble: hud.matDoc.doubleSided = value; break;
            case kFlMatAlpha: hud.matDoc.alphaCut = value; break;
            case kFlMatFlip: hud.matDoc.flipNormalY = value; break;
            case kFlMatUnlit: hud.matDoc.unlit = value; break;
            case kFlMatRepeat: hud.matDoc.repeat = value; break;
            case kFlMatPack: hud.matDoc.mapPack = value; break;
            case kFlLiveWorld: hud.live.worldAligned = value; break;
            case kFlLiveDouble: hud.live.doubleSided = value; break;
            case kFlLiveAlpha: hud.live.alphaCut = value; break;
            case kFlLiveFlip: hud.live.flipNormalY = value; break;
            case kFlLiveUnlit: hud.live.unlit = value; break;
            default: break;
            }
            continue;
        }
        const uint16_t count = row.kind == kBindSwatch ? 3u : 1u;
        for (uint16_t step = 0; step < count; ++step) {
            const uint16_t index = static_cast<uint16_t>(row.index + step);
            if (index < 256u) {
                sceneBlockPut(hud, index, hud.block.scalars[index]);
            }
        }
    }
}

} // namespace burnhope
