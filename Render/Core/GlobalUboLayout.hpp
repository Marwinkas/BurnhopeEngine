#pragma once

#include <cstddef>

#include "../../Utils/FrameInfo.hpp"

namespace burnhope {

/** SPIR-V GlobalUbo_std140 from gbuffer.frag.spv (std140). */
inline constexpr std::size_t kGlobalUboStd140Size = 2560u;
inline constexpr std::size_t kGlobalUboOffsetCamPos = 192u;
inline constexpr std::size_t kGlobalUboOffsetZNear = 204u;
inline constexpr std::size_t kGlobalUboOffsetSunDir = 224u;
inline constexpr std::size_t kGlobalUboOffsetZFar = 236u;
inline constexpr std::size_t kGlobalUboOffsetScreenSize = 240u;
inline constexpr std::size_t kGlobalUboOffsetSunLightMatrices = 256u;
inline constexpr std::size_t kGlobalUboOffsetCascadeSplits = 512u;
inline constexpr std::size_t kGlobalUboOffsetGridDimX = 528u;
inline constexpr std::size_t kGlobalUboOffsetPortalId = 540u;
inline constexpr std::size_t kGlobalUboOffsetLightSize = 544u;
inline constexpr std::size_t kGlobalUboOffsetSunColor = 560u;
inline constexpr std::size_t kGlobalUboOffsetSunIntensity = 572u;
inline constexpr std::size_t kGlobalUboOffsetPpPsych2 = 2432u;
inline constexpr std::size_t kGlobalUboOffsetPpTexIndices = 2544u;

static_assert(sizeof(GlobalUbo) == kGlobalUboStd140Size, "GlobalUbo CPU size != SPIR-V std140");
static_assert(offsetof(GlobalUbo, camPos) == kGlobalUboOffsetCamPos);
static_assert(offsetof(GlobalUbo, zNear) == kGlobalUboOffsetZNear);
static_assert(offsetof(GlobalUbo, sunDir) == kGlobalUboOffsetSunDir);
static_assert(offsetof(GlobalUbo, zFar) == kGlobalUboOffsetZFar);
static_assert(offsetof(GlobalUbo, screenSize) == kGlobalUboOffsetScreenSize);
static_assert(offsetof(GlobalUbo, sunLightSpaceMatrices) == kGlobalUboOffsetSunLightMatrices);
static_assert(offsetof(GlobalUbo, cascadeSplits) == 512u);
static_assert(offsetof(GlobalUbo, gridDimX) == kGlobalUboOffsetGridDimX);
static_assert(offsetof(GlobalUbo, portalID) == kGlobalUboOffsetPortalId);
static_assert(offsetof(GlobalUbo, lightSize) == kGlobalUboOffsetLightSize);
static_assert(offsetof(GlobalUbo, sunColor) == kGlobalUboOffsetSunColor);
static_assert(offsetof(GlobalUbo, sunIntensity) == kGlobalUboOffsetSunIntensity);
static_assert(offsetof(GlobalUbo, ppPsych2) == kGlobalUboOffsetPpPsych2);
static_assert(offsetof(GlobalUbo, ppTexIndices) == kGlobalUboOffsetPpTexIndices);

} // namespace burnhope
