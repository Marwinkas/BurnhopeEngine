#pragma once

#include "gfx/Material.hpp"
#include "render/SceneBlock.hpp"
#include "render/SceneFrame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

// A window-block scalar and the byte offset of the float it mirrors inside a record.
struct ScalarField {
    uint16_t scalar = 0;
    uint16_t offset = 0;
};

inline constexpr uint16_t kNoField = 0xFFFF;
using ScalarMap = std::array<uint16_t, 256>;

// Scalar index -> record offset, kNoField where the record holds no such scalar.
template <std::size_t N>
[[nodiscard]] constexpr ScalarMap scalarMapOf(const ScalarField (&fields)[N]) {
    ScalarMap map{};
    for (uint16_t& slot : map) {
        slot = kNoField;
    }
    for (const ScalarField& field : fields) {
        map[field.scalar] = field.offset;
    }
    return map;
}

template <class T>
[[nodiscard]] float scalarLoad(const T& record, uint16_t offset) {
    static_assert(std::is_trivially_copyable_v<T>);
    float value = 0.0f;
    std::memcpy(&value, static_cast<const std::byte*>(static_cast<const void*>(&record)) + offset, sizeof(float));
    return value;
}

template <class T>
void scalarSet(T& record, uint16_t offset, float value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(static_cast<std::byte*>(static_cast<void*>(&record)) + offset, &value, sizeof(float));
}

// Writes `value` into the float of `record` that `index` maps to. False when the record has no such scalar.
template <class T>
bool scalarStore(T& record, const ScalarMap& map, uint16_t index, float value) {
    if (index >= map.size() || map[index] == kNoField) {
        return false;
    }
    scalarSet(record, map[index], value);
    return true;
}

static_assert(std::is_standard_layout_v<SceneTweaks>);
static_assert(std::is_standard_layout_v<BhmatDoc>);

// Every float scalar of SceneTweaks. Integer and bool tweaks are packed through the flag block instead.
inline constexpr ScalarField kTweakFields[] = {
    {kScGtaoRadius, offsetof(SceneTweaks, gtaoRadius)},
    {kScGtaoSteps, offsetof(SceneTweaks, gtaoSteps)},
    {kScGtaoSlices, offsetof(SceneTweaks, gtaoSlices)},
    {kScGtaoPower, offsetof(SceneTweaks, gtaoPower)},
    {kScGtaoFalloff, offsetof(SceneTweaks, gtaoFalloff)},
    {kScGtaoDenoise, offsetof(SceneTweaks, gtaoDenoise)},
    {kScSsrCutoff, offsetof(SceneTweaks, ssrCutoff)},
    {kScSsrSteps, offsetof(SceneTweaks, ssrSteps)},
    {kScSsrStride, offsetof(SceneTweaks, ssrStride)},
    {kScSsrThick, offsetof(SceneTweaks, ssrThick)},
    {kScSsrMetal, offsetof(SceneTweaks, ssrMetal)},
    {kScSsrRt, offsetof(SceneTweaks, ssrRt)},
    {kScSsrEdge, offsetof(SceneTweaks, ssrEdge)},
    {kScSsrMax, offsetof(SceneTweaks, ssrMax)},
    {kScSsrBias, offsetof(SceneTweaks, ssrBias)},
    {kScSscsReach, offsetof(SceneTweaks, sscsReach)},
    {kScSscsThick, offsetof(SceneTweaks, sscsThick)},
    {kScSscsSteps, offsetof(SceneTweaks, sscsSteps)},
    {kScSscsPx, offsetof(SceneTweaks, sscsPx)},
    {kScSsrcBase, offsetof(SceneTweaks, ssrcBase)},
    {kScSsrcThick, offsetof(SceneTweaks, ssrcThick)},
    {kScSsrcCap, offsetof(SceneTweaks, ssrcCap)},
    {kScSsrcSteps, offsetof(SceneTweaks, ssrcSteps)},
    {kScRcRays, offsetof(SceneTweaks, rcRays)},
    {kScRcSpacing, offsetof(SceneTweaks, rcSpacing)},
    {kScRcIntensity, offsetof(SceneTweaks, rcIntensity)},
    {kScRcMax, offsetof(SceneTweaks, rcMax)},
    {kScRcSun, offsetof(SceneTweaks, rcSun)},
    {kScRcBounce, offsetof(SceneTweaks, rcBounce)},
    {kScGiGain, offsetof(SceneTweaks, giGain)},
    {kScRestirLocal, offsetof(SceneTweaks, restirLocal)},
    {kScRestirSpatial, offsetof(SceneTweaks, restirSpatial)},
    {kScRestirRadius, offsetof(SceneTweaks, restirRadius)},
    {kScRestirHistory, offsetof(SceneTweaks, restirHistory)},
    {kScRestirBoil, offsetof(SceneTweaks, restirBoil)},
    {kScRestirTemporalDepth, offsetof(SceneTweaks, restirTemporalDepth)},
    {kScRestirTemporalNormal, offsetof(SceneTweaks, restirTemporalNormal)},
    {kScRestirTemporalBias, offsetof(SceneTweaks, restirTemporalBias)},
    {kScRestirSpatialDepth, offsetof(SceneTweaks, restirSpatialDepth)},
    {kScRestirSpatialNormal, offsetof(SceneTweaks, restirSpatialNormal)},
    {kScRestirSpatialBias, offsetof(SceneTweaks, restirSpatialBias)},
    {kScRestirSpatialBoost, offsetof(SceneTweaks, restirSpatialBoost)},
    {kScRestirSunDisk, offsetof(SceneTweaks, restirSunDisk)},
    {kScRestirSunRays, offsetof(SceneTweaks, restirSunRays)},
    {kScRestirShadowRays, offsetof(SceneTweaks, restirShadowRays)},
    {kScRestirShadowRange, offsetof(SceneTweaks, restirShadowRange)},
    {kScRestirShadowBias, offsetof(SceneTweaks, restirShadowBias)},
    {kScRestirPermTh, offsetof(SceneTweaks, restirPermTh)},
    {kScRestirTarget, offsetof(SceneTweaks, restirTarget)},
    {kScFsrSharpness, offsetof(SceneTweaks, fsrSharpness)},
    {kScBloomThreshold, offsetof(SceneTweaks, bloomThreshold)},
    {kScFogDensity, offsetof(SceneTweaks, fogDensity)},
    {kScFogHeight, offsetof(SceneTweaks, fogHeight)},
    {kScFogScatter, offsetof(SceneTweaks, fogScatter)},
    {kScFogFalloff, offsetof(SceneTweaks, fogFalloff)},
    {kScFogR, offsetof(SceneTweaks, fogR)},
    {kScFogG, offsetof(SceneTweaks, fogG)},
    {kScFogB, offsetof(SceneTweaks, fogB)},
    {kScFogLum, offsetof(SceneTweaks, fogLum)},
    {kScFogStart, offsetof(SceneTweaks, fogStart)},
    {kScFogOpacity, offsetof(SceneTweaks, fogOpacity)},
    {kScFogInStart, offsetof(SceneTweaks, fogInStart)},
    {kScRainAmount, offsetof(SceneTweaks, rainAmount)},
    {kScRainWet, offsetof(SceneTweaks, rainWet)},
    {kScRainRipple, offsetof(SceneTweaks, rainRipple)},
    {kScSunZenith, offsetof(SceneTweaks, sunZenith)},
    {kScSunAzimuth, offsetof(SceneTweaks, sunAzimuth)},
    {kScCloudCoverage, offsetof(SceneTweaks, cloudCoverage)},
    {kScCloudDensity, offsetof(SceneTweaks, cloudDensity)},
    {kScWindSpeed, offsetof(SceneTweaks, windSpeed)},
    {kScWindYaw, offsetof(SceneTweaks, windYaw)},
    {kScTemperatureC, offsetof(SceneTweaks, temperatureC)},
    {kScDayLength, offsetof(SceneTweaks, dayLength)},
    {kScTimeOfDay, offsetof(SceneTweaks, timeOfDay)},
    {kScSunKelvin, offsetof(SceneTweaks, sunKelvin)},
    {kScSunAngle, offsetof(SceneTweaks, sunAngle)},
    {kScSunSoft, offsetof(SceneTweaks, sunSoft)},
    {kScSunIndirect, offsetof(SceneTweaks, sunIndirect)},
};

// The .bhmat scalars that copy one float of the open material. Emissive gain and UV rotation convert, so they are
// handled where they are used.
inline constexpr ScalarField kDocFields[] = {
    {kScMatBase0, offsetof(BhmatDoc, base) + 0 * sizeof(float)},
    {kScMatBase1, offsetof(BhmatDoc, base) + 1 * sizeof(float)},
    {kScMatBase2, offsetof(BhmatDoc, base) + 2 * sizeof(float)},
    {kScMatRough, offsetof(BhmatDoc, roughness)},
    {kScMatMetal, offsetof(BhmatDoc, metallic)},
    {kScMatEmissive, offsetof(BhmatDoc, emissive)},
    {kScMatAlbedoGain, offsetof(BhmatDoc, albedoGain)},
    {kScMatNormalGain, offsetof(BhmatDoc, normalGain)},
    {kScMatOrmGain, offsetof(BhmatDoc, ormGain)},
    {kScMatReflect, offsetof(BhmatDoc, reflectance)},
    {kScMatAo, offsetof(BhmatDoc, aoStrength)},
    {kScMatNormal, offsetof(BhmatDoc, normalScale)},
    {kScMatCoat, offsetof(BhmatDoc, clearcoat)},
    {kScMatCoatRough, offsetof(BhmatDoc, clearcoatRoughness)},
    {kScMatGrid, offsetof(BhmatDoc, worldGridSize)},
    {kScMatSharp, offsetof(BhmatDoc, triplanarSharpness)},
    {kScMatUvX, offsetof(BhmatDoc, uvTiling) + 0 * sizeof(float)},
    {kScMatUvY, offsetof(BhmatDoc, uvTiling) + 1 * sizeof(float)},
    {kScMatCutoff, offsetof(BhmatDoc, alphaCutoff)},
    {kScMatEm0, offsetof(BhmatDoc, emissiveColor) + 0 * sizeof(float)},
    {kScMatEm1, offsetof(BhmatDoc, emissiveColor) + 1 * sizeof(float)},
    {kScMatEm2, offsetof(BhmatDoc, emissiveColor) + 2 * sizeof(float)},
    {kScMatHeight, offsetof(BhmatDoc, heightScale)},
    {kScMatOpacity, offsetof(BhmatDoc, baseA)},
    {kScMatUvOffX, offsetof(BhmatDoc, uvOffset) + 0 * sizeof(float)},
    {kScMatUvOffY, offsetof(BhmatDoc, uvOffset) + 1 * sizeof(float)},
};

// The same for the live record of the selected actor's material.
inline constexpr ScalarField kLiveFields[] = {
    {kScLiveBase0, offsetof(BhmatDoc, base) + 0 * sizeof(float)},
    {kScLiveBase1, offsetof(BhmatDoc, base) + 1 * sizeof(float)},
    {kScLiveBase2, offsetof(BhmatDoc, base) + 2 * sizeof(float)},
    {kScLiveEm0, offsetof(BhmatDoc, emissiveColor) + 0 * sizeof(float)},
    {kScLiveEm1, offsetof(BhmatDoc, emissiveColor) + 1 * sizeof(float)},
    {kScLiveEm2, offsetof(BhmatDoc, emissiveColor) + 2 * sizeof(float)},
    {kScLiveRough, offsetof(BhmatDoc, roughness)},
    {kScLiveMetal, offsetof(BhmatDoc, metallic)},
    {kScLiveReflect, offsetof(BhmatDoc, reflectance)},
    {kScLiveAo, offsetof(BhmatDoc, aoStrength)},
    {kScLiveNormal, offsetof(BhmatDoc, normalScale)},
    {kScLiveEmGain, offsetof(BhmatDoc, emissiveGain)},
    {kScLiveCoat, offsetof(BhmatDoc, clearcoat)},
    {kScLiveCoatRough, offsetof(BhmatDoc, clearcoatRoughness)},
    {kScLiveAlpha, offsetof(BhmatDoc, alphaCutoff)},
    {kScLiveGrid, offsetof(BhmatDoc, worldGridSize)},
    {kScLiveSharp, offsetof(BhmatDoc, triplanarSharpness)},
    {kScLiveOff0, offsetof(BhmatDoc, worldOffset) + 0 * sizeof(float)},
    {kScLiveOff1, offsetof(BhmatDoc, worldOffset) + 1 * sizeof(float)},
    {kScLiveOff2, offsetof(BhmatDoc, worldOffset) + 2 * sizeof(float)},
    {kScLiveUvX, offsetof(BhmatDoc, uvTiling) + 0 * sizeof(float)},
    {kScLiveUvY, offsetof(BhmatDoc, uvTiling) + 1 * sizeof(float)},
    {kScLiveUvOffX, offsetof(BhmatDoc, uvOffset) + 0 * sizeof(float)},
    {kScLiveUvOffY, offsetof(BhmatDoc, uvOffset) + 1 * sizeof(float)},
};

inline constexpr ScalarMap kTweakMap = scalarMapOf(kTweakFields);
inline constexpr ScalarMap kDocMap = scalarMapOf(kDocFields);
inline constexpr ScalarMap kLiveMap = scalarMapOf(kLiveFields);

} // namespace burnhope
