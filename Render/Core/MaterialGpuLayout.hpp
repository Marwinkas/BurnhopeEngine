#pragma once

#include <cstddef>

#include "SceneGpuTypes.hpp"

namespace burnhope {

/** SPIR-V MaterialData_natural / std430 (gbuffer.frag.spv, stride 112). */
static_assert(sizeof(MaterialData) == 112u);
static_assert(offsetof(MaterialData, albedoAlphaIdx) == 0u);
static_assert(offsetof(MaterialData, normalIdx) == 4u);
static_assert(offsetof(MaterialData, uvScale) == 32u);
static_assert(offsetof(MaterialData, triplanarScale) == 40u);
static_assert(offsetof(MaterialData, emissiveIntensity) == 44u);
static_assert(offsetof(MaterialData, albedoColor) == 48u);
static_assert(offsetof(MaterialData, metallicStrength) == 80u);
static_assert(offsetof(MaterialData, pad4) == 108u);

} // namespace burnhope
