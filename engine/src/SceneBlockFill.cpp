#include "render/SceneBlockFill.hpp"
#include "render/SceneBlockMap.hpp"

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

} // namespace burnhope
