#pragma once

#include "../../Utils/DirectXMathCompat.hpp"
#include <cstddef>
#include <cstdint>

namespace burnhope {

/**
 * std430 layout — offsets must match SPIR-V ObjectData_std430 (gbuffer.mesh.spv).
 * Do not add alignas(16) on the struct; trailing padding is explicit.
 */
struct ObjectData {
  float4x4 modelMatrix;
  uint32_t materialID;
  uint32_t indexCount;
  uint32_t vrsRate;
  uint32_t boneOffset;
  uint32_t posHeap;
  uint32_t attrHeap;
  uint32_t indexHeap;
  uint32_t vertexCount;
  uint64_t colorBufferAddress;
  uint64_t uv2BufferAddress;
  uint64_t animBufferAddress;
  uint32_t _pad120[2];
  float4 aabbMin;
  float4 aabbMax;
  uint32_t pad0{0};
  uint32_t pad1{0};
  uint32_t _pad168[2];
  uint32_t _pad192[4];
};

/** std430 — no struct alignas; offsets verified in MaterialGpuLayout.hpp */
struct MaterialData {
  int32_t albedoAlphaIdx;
  int32_t normalIdx;
  int32_t ormxIdx;
  int32_t emissiveIdx;
  int32_t useTriplanar;
  int32_t isTransparent;
  int32_t repeatTexture;
  int32_t pad1;
  float2 uvScale;
  float triplanarScale;
  float emissiveIntensity;
  float4 albedoColor;
  float4 emissiveColor;
  float metallicStrength;
  float roughnessStrength;
  float normalStrength;
  float heightStrength;
  float aoStrength;
  float pad2;
  float pad3;
  float pad4;
};

static_assert(sizeof(MaterialData) % 16 == 0, "MaterialData must be 16-byte aligned");
/** Must match kMaterialDataStride in shaders/common/SceneGpu.slang */
inline constexpr uint32_t kGpuMaterialDataStride = static_cast<uint32_t>(sizeof(MaterialData));
static_assert(kGpuMaterialDataStride == 112u, "sync kMaterialDataStride in FragBda.slang");
static_assert(offsetof(ObjectData, colorBufferAddress) == 96);
static_assert(offsetof(ObjectData, animBufferAddress) == 112);
static_assert(offsetof(ObjectData, aabbMin) == 128);
static_assert(offsetof(ObjectData, pad0) == 160);
static_assert(offsetof(ObjectData, _pad192) == 176);
static_assert(sizeof(ObjectData) == 192, "ObjectData stride must match SPIR-V ArrayStride 192");

} // namespace burnhope
