#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace burnhope {

// One window block. Modes are bytes. Sliders, pose, and post are one float array.
// A pass reads a contiguous range. Names of inspector rows stay in the index, not in the pass.
struct alignas(16) SceneBlock {
    uint8_t flags[64]{};
    uint32_t passMask = 63u;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
    float scalars[256]{};
};

static_assert(sizeof(SceneBlock) % 16 == 0);
static_assert(offsetof(SceneBlock, scalars) % 16 == 0);
static_assert(offsetof(SceneBlock, flags) == 0);

enum SceneFlag : uint16_t {
    kFlHiz = 0,
    kFlCone,
    kFlLod,
    kFlFrust,
    kFlSmall,
    kFlBackf,
    kFlScrn,
    kFlBehind,
    kFlShadowCull,
    kFlProbes,
    kFlCluster,
    kFlPoints,
    kFlSpots,
    kFlArea,
    kFlSky,
    kFlCloud,
    kFlFog,
    kFlBloom,
    kFlContact,
    kFlVt,
    kFlLumScale,
    kFlGamut,
    kFlSun,
    kFlSunCast,
    kFlWxClock,
    kFlTone,
    kFlAa,
    kFlFsrQ,
    kFlGi,
    kFlDirect,
    kFlReflect,
    kFlAo,
    kFlSunShadow,
    kFlView,
    kFlSharcDenoise,
    kFlRestirTemporal,
    kFlRestirSpatial,
    kFlRestirBoil,
    kFlRestirInitVis,
    kFlRestirPerm,
    kFlRestirShortcut,
    kFlRestirMaterial,
    kFlRestirNaive,
    kFlRestirSun,
    kFlFreeze,
    kFlSunCsm,
    kFlActorStatic,
    kFlMatWorld,
    kFlMatDouble,
    kFlMatAlpha,
    kFlLiveWorld,
    kFlLiveDouble,
    kFlLiveAlpha,
    kFlLiveFlip,
    kFlLiveUnlit,
    kFlMatFlip,
    kFlMatUnlit,
    kFlMatRepeat,
    kFlMatPack,
    kFlCount,
};

static_assert(kFlCount <= 64);

// Scalar order matches the packed ranges a pass copies. Post is 39 floats, the same order as HudPost.
enum SceneScalar : uint16_t {
    kScGrain = 0,
    kScBrightness,
    kScBloom,
    kScVignette,
    kScCa,
    kScContact,
    kScSpecOcc,
    kScToksvig,
    kScPom,
    kScGgx,
    kScSsrcAmt,
    kScGtaoAmt,
    kScLamp,
    kScSunGain,
    kScSunR,
    kScSunG,
    kScSunB,
    kScExposure,
    kScTemperature,
    kScTint,
    kScContrast,
    kScSaturation,
    kScNight,
    kScShadows,
    kScMidtones,
    kScHighlights,
    kScSlope,
    kScCdlOffset,
    kScPower,
    kScDofFocus,
    kScDofAmount,
    kScFlare,
    kScGradeFlags,
    kScMix,
    kScGenContrast,
    kScMidIn,
    kScMidOut,
    kScHdrMax,
    kScVibrance,
    kScGtaoRadius,
    kScGtaoSteps,
    kScGtaoSlices,
    kScGtaoPower,
    kScGtaoFalloff,
    kScGtaoDenoise,
    kScSsrCutoff,
    kScSsrSteps,
    kScSsrStride,
    kScSsrThick,
    kScSsrMetal,
    kScSsrRt,
    kScSsrEdge,
    kScSsrMax,
    kScSsrBias,
    kScSscsReach,
    kScSscsThick,
    kScSscsSteps,
    kScSscsPx,
    kScSsrcBase,
    kScSsrcThick,
    kScSsrcCap,
    kScSsrcSteps,
    kScRcRays,
    kScRcSpacing,
    kScRcIntensity,
    kScRcMax,
    kScRcSun,
    kScRcBounce,
    kScGiGain,
    kScRestirLocal,
    kScRestirSpatial,
    kScRestirRadius,
    kScRestirHistory,
    kScRestirBoil,
    kScRestirTemporalDepth,
    kScRestirTemporalNormal,
    kScRestirTemporalBias,
    kScRestirSpatialDepth,
    kScRestirSpatialNormal,
    kScRestirSpatialBias,
    kScRestirSpatialBoost,
    kScRestirSunDisk,
    kScRestirSunRays,
    kScRestirShadowRays,
    kScRestirShadowRange,
    kScRestirShadowBias,
    kScRestirPermTh,
    kScRestirTarget,
    kScFsrSharpness,
    kScBloomThreshold,
    kScFogDensity,
    kScFogHeight,
    kScFogScatter,
    kScFogFalloff,
    kScFogR,
    kScFogG,
    kScFogB,
    kScFogLum,
    kScFogStart,
    kScFogOpacity,
    kScFogInStart,
    kScRainAmount,
    kScRainWet,
    kScRainRipple,
    kScSunZenith,
    kScSunAzimuth,
    kScCloudCoverage,
    kScCloudDensity,
    kScWindSpeed,
    kScWindYaw,
    kScTemperatureC,
    kScDayLength,
    kScTimeOfDay,
    kScSunKelvin,
    kScSunAngle,
    kScSunSoft,
    kScSunIndirect,
    kScColor0,
    kScColor1,
    kScColor2,
    kScRough,
    kScMetal,
    kScEmissive,
    kScPos0,
    kScPos1,
    kScPos2,
    kScEuler0,
    kScEuler1,
    kScEuler2,
    kScScale0,
    kScScale1,
    kScScale2,
    kScPivot0,
    kScPivot1,
    kScPivot2,
    kScForceLod,
    kScMatBase0,
    kScMatBase1,
    kScMatBase2,
    kScMatRough,
    kScMatMetal,
    kScMatEmissive,
    kScMatAlbedoGain,
    kScMatNormalGain,
    kScMatOrmGain,
    kScMatEmissiveGain,
    kScMatReflect,
    kScMatAo,
    kScMatNormal,
    kScMatCoat,
    kScMatCoatRough,
    kScMatGrid,
    kScMatSharp,
    kScMatUvX,
    kScMatUvY,
    kScMatUvRot,
    kScLiveBase0,
    kScLiveBase1,
    kScLiveBase2,
    kScLiveEm0,
    kScLiveEm1,
    kScLiveEm2,
    kScLiveRough,
    kScLiveMetal,
    kScLiveReflect,
    kScLiveAo,
    kScLiveNormal,
    kScLiveEmGain,
    kScLiveCoat,
    kScLiveCoatRough,
    kScLiveAlpha,
    kScLiveGrid,
    kScLiveSharp,
    kScLiveOff0,
    kScLiveOff1,
    kScLiveOff2,
    kScLiveUvX,
    kScLiveUvY,
    kScLiveUvOffX,
    kScLiveUvOffY,
    kScUvDeg,
    kScMatEm0,
    kScMatEm1,
    kScMatEm2,
    kScMatHeight,
    kScMatOpacity,
    kScMatUvOffX,
    kScMatUvOffY,
    kScMatCutoff,
    kScCount,
};

constexpr uint16_t kScPost = kScGrain;
constexpr uint16_t kScPostN = 39;
constexpr uint16_t kScGtao = kScGtaoRadius;
constexpr uint16_t kScGtaoN = 6;
constexpr uint16_t kScSsr = kScSsrCutoff;
constexpr uint16_t kScSsrN = 9;
constexpr uint16_t kScSscs = kScSscsReach;
constexpr uint16_t kScSscsN = 4;
constexpr uint16_t kScSsrc = kScSsrcBase;
constexpr uint16_t kScSsrcN = 4;
constexpr uint16_t kScRc = kScRcRays;
constexpr uint16_t kScRcN = 7;
constexpr uint16_t kScRestir = kScRestirLocal;
constexpr uint16_t kScRestirN = 19;
constexpr uint16_t kScFog = kScFogDensity;
constexpr uint16_t kScFogN = 11;
constexpr uint16_t kScActor = kScColor0;
constexpr uint16_t kScActorN = 18;

static_assert(kScCount <= 256);
static_assert(kScVibrance + 1 == kScPost + kScPostN);
static_assert(kScGtaoDenoise + 1 == kScGtao + kScGtaoN);
static_assert(kScSsrBias + 1 == kScSsr + kScSsrN);
static_assert(kScSscsPx + 1 == kScSscs + kScSscsN);
static_assert(kScSsrcSteps + 1 == kScSsrc + kScSsrcN);
static_assert(kScGiGain + 1 == kScRc + kScRcN);
static_assert(kScRestirTarget + 1 == kScRestir + kScRestirN);
static_assert(kScFogInStart + 1 == kScFog + kScFogN);
static_assert(kScPivot2 + 1 == kScActor + kScActorN);
static_assert(kScMatCutoff + 1 == kScCount);

[[nodiscard]] inline std::span<const float> sceneBlockSpan(const SceneBlock& block, uint16_t begin, uint16_t count) {
    if (static_cast<uint32_t>(begin) + count > 256u) {
        return {};
    }
    return {block.scalars + begin, count};
}

[[nodiscard]] inline std::span<float> sceneBlockSpan(SceneBlock& block, uint16_t begin, uint16_t count) {
    if (static_cast<uint32_t>(begin) + count > 256u) {
        return {};
    }
    return {block.scalars + begin, count};
}

[[nodiscard]] inline float sceneBlockAt(const SceneBlock& block, uint16_t index) {
    return index < 256u ? block.scalars[index] : 0.0f;
}

[[nodiscard]] inline uint8_t sceneBlockFlag(const SceneBlock& block, uint16_t index) {
    return index < 64u ? block.flags[index] : 0u;
}

} // namespace burnhope
