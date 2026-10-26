#include "app/SceneBlockFill.hpp"
#include "app/SceneBlockMap.hpp"

#include <cstring>
#include <type_traits>

namespace burnhope {

void sceneBlockFill(SceneBlock& block, const HudPost& post, const SceneTweaks& tweaks, const HudFlags& flags, const HudActor& actor) {
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

    for (const ScalarField& field : kTweakFields) {
        block.scalars[field.scalar] = scalarLoad(tweaks, field.offset);
    }
    block.scalars[kScForceLod] = static_cast<float>(tweaks.forceLod);

    static_assert(std::is_standard_layout_v<HudActor>);
    static_assert(offsetof(HudActor, color) == 0);
    static_assert(offsetof(HudActor, pivot) == 15 * sizeof(float));
    static_assert(offsetof(HudActor, isStatic) == kScActorN * sizeof(float));
    std::memcpy(block.scalars + kScActor, &actor, kScActorN * sizeof(float));
}

// HudPost and HudActor are contiguous float blocks laid out in scalar order (asserted in sceneBlockFill), so a
// scalar index is a float index into the record.
void sceneBlockPutPost(HudPost& post, uint16_t index, float value) {
    static_assert(std::is_standard_layout_v<HudPost>);
    if (index < kScPost || index >= kScPost + kScPostN) {
        return;
    }
    float flat[kScPostN];
    std::memcpy(flat, &post, sizeof(flat));
    flat[index - kScPost] = value;
    std::memcpy(static_cast<void*>(&post), flat, sizeof(flat));
}

void sceneBlockPutActor(HudActor& actor, uint16_t index, float value) {
    static_assert(std::is_standard_layout_v<HudActor>);
    if (index < kScActor || index >= kScActor + kScActorN) {
        return;
    }
    float flat[kScActorN];
    std::memcpy(flat, &actor, sizeof(flat));
    flat[index - kScActor] = value;
    std::memcpy(static_cast<void*>(&actor), flat, sizeof(flat));
}

void sceneBlockPut(HudContext& hud, uint16_t index, float value) {
    if (index < kScPostN) {
        sceneBlockPutPost(hud.post, index, value);
        return;
    }
    if (index >= kScColor0 && index <= kScPivot2) {
        sceneBlockPutActor(hud.actor, index, value);
        return;
    }
    if (scalarStore(hud.tweaks, kTweakMap, index, value) || scalarStore(hud.matDoc, kDocMap, index, value) ||
        scalarStore(hud.live, kLiveMap, index, value)) {
        return;
    }
    switch (index) {
    case kScForceLod: hud.flags.forceLod = value; break;
    case kScMatEmissiveGain: hud.matDoc.emissiveGain = value; hud.matDoc.emissive = value; break;
    case kScMatUvRot: {
        const float rad = value * 0.01745329252f;
        const float delta = rad - hud.matDoc.uvRotation;
        if (delta > 0.0001f || delta < -0.0001f) {
            hud.matDoc.uvRotation = rad;
        }
        break;
    }
    case kScUvDeg: hud.uvDeg = value; break;
    default: break;
    }
}

void sceneBlockApply(HudContext& hud) {
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
