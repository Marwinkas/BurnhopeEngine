#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "BindlessRegistry.hpp"
#include "Device.hpp"
#include "FrameInfo.hpp"
#include "DirectXMathCompat.hpp"
#include "../Render/RenderDebug.hpp"
#include "../Render/Core/SceneAddresses.hpp"
#include "../Render/Core/SceneGpuTypes.hpp"

namespace burnhope {

inline uint32_t heapBindingIndex(uint32_t slot, const BurnhopeDevice& device, VkDescriptorType type) noexcept {
  if (slot == 0u) {
    return 0u;
  }
  const VkDeviceSize heapStride = device.resourceHeapDescriptorStride();
  const VkDeviceSize descBytes = device.descriptorSize(type);
  if (descBytes == 0u) {
    return slot;
  }
  const VkDeviceSize byteOffset = static_cast<VkDeviceSize>(slot) * heapStride;
  return static_cast<uint32_t>(byteOffset / descBytes);
}

inline uint32_t heapSsboBindingIndex(uint32_t slot, const BurnhopeDevice& device) noexcept {
  return heapBindingIndex(slot, device, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

inline uint32_t heapUboBindingIndex(uint32_t slot, const BurnhopeDevice& device) noexcept {
  return heapBindingIndex(slot, device, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
}

// --- Wire layout = shaders/common/GfxHeapPC.slang / SPIR-V (260 bytes, [0,260) per validation) ---

/** Must match gbuffer SPIR-V GfxHeapPC_std430 and device maxPushDataSize (typically 256). */
inline constexpr uint32_t kGfxHeapPushBytes = 256u;

// Wire offsets = OpMemberDecorate in compiled SPIR-V (not C++ struct offsetof).
inline constexpr uint32_t kGfxWireHeapResourceBase = 180u;
inline constexpr uint32_t kGfxWireGlobalUboHeap = 184u;
inline constexpr uint32_t kGfxWireDefaultSampler = 188u;
inline constexpr uint32_t kGfxWireTextureTableCount = 192u;
inline constexpr uint32_t kGfxWireMaterialStorageBda = 200u;
inline constexpr uint32_t kGfxWirePad208 = 208u;

/** CPU-side fields (any natural layout); pack with packGfxHeapPush before vkCmdPushDataEXT. */
struct GfxHeapPC {
  float4x4 meshProjection{};
  float4x4 meshView{};
  uint64_t sceneAddressesBda{0};
  uint64_t meshPosBda{0};
  uint64_t meshAttrBda{0};
  uint64_t meshIdxBda{0};
  uint32_t gfxDebug{0};
  uint32_t cpuLodTriCount{0};
  uint32_t meshVertexCount{0};
  uint32_t meshIndexBase{0};
  uint32_t meshMaterialId{0};
  uint32_t heapResourceBase{0};
  uint32_t globalUboHeap{0};
  uint32_t defaultSampler{0};
  uint32_t textureTableCount{0};
  uint32_t _padBeforeMaterialBda{0};
  uint64_t materialStorageBda{0};
  uint32_t _pad208[12]{};
};

static_assert(offsetof(GfxHeapPC, meshView) == 64);
static_assert(offsetof(GfxHeapPC, sceneAddressesBda) == 128);
static_assert(offsetof(GfxHeapPC, cpuLodTriCount) == 164);
static_assert(offsetof(GfxHeapPC, meshVertexCount) == 168);
static_assert(kGfxWireGlobalUboHeap == 184u);
static_assert(kGfxWireMaterialStorageBda == 200u);
static_assert(kGfxWirePad208 + 48u == kGfxHeapPushBytes);
static_assert(offsetof(GfxHeapPC, globalUboHeap) == kGfxWireGlobalUboHeap);
static_assert(offsetof(GfxHeapPC, materialStorageBda) == kGfxWireMaterialStorageBda);

inline void packGfxHeapPush(uint8_t* out, const GfxHeapPC& pc) noexcept {
  std::memcpy(out + 0, &pc.meshProjection, 64);
  std::memcpy(out + 64, &pc.meshView, 64);
  std::memcpy(out + 128, &pc.sceneAddressesBda, 8);
  std::memcpy(out + 136, &pc.meshPosBda, 8);
  std::memcpy(out + 144, &pc.meshAttrBda, 8);
  std::memcpy(out + 152, &pc.meshIdxBda, 8);
  std::memcpy(out + 160, &pc.gfxDebug, 4);
  std::memcpy(out + 164, &pc.cpuLodTriCount, 4);
  std::memcpy(out + 168, &pc.meshVertexCount, 4);
  std::memcpy(out + 172, &pc.meshIndexBase, 4);
  std::memcpy(out + 176, &pc.meshMaterialId, 4);
  std::memcpy(out + kGfxWireHeapResourceBase, &pc.heapResourceBase, 4);
  std::memcpy(out + kGfxWireGlobalUboHeap, &pc.globalUboHeap, 4);
  std::memcpy(out + kGfxWireDefaultSampler, &pc.defaultSampler, 4);
  std::memcpy(out + kGfxWireTextureTableCount, &pc.textureTableCount, 4);
  std::memcpy(out + 196, &pc._padBeforeMaterialBda, 4);
  std::memcpy(out + kGfxWireMaterialStorageBda, &pc.materialStorageBda, 8);
  std::memcpy(out + kGfxWirePad208, pc._pad208, 48);
  const uint32_t heapResourceBaseZero = 0u;
  std::memcpy(out + kGfxWireHeapResourceBase, &heapResourceBaseZero, 4);
}

struct ShadowVertPC {
  float4x4 lightSpaceMatrix{};
  uint32_t objectStorage{0};
  uint32_t boneStorage{0};
};
static_assert(sizeof(ShadowVertPC) == 72);

struct PortalVertPC {
  float4x4 modelMatrix{};
  uint32_t portalId{0};
  uint32_t globalUbo{0};
};

struct LightingHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferNormal{0};
  uint32_t gbufferAlbedo{0};
  uint32_t gbufferHeightAo{0};
  uint32_t gbufferDepth{0};
  uint32_t gbufferEmissive{0};
  uint32_t gbufferPortalId{0};
  uint32_t shadowCsm{0};
  uint32_t shadowAtlas{0};
  uint32_t blueNoise{0};
  uint32_t lightBuffer{0};
  uint32_t lightGrid{0};
  uint32_t lightIndexList{0};
  uint32_t faceMatrices{0};
  uint32_t decalBuffer{0};
  uint32_t hdrOutput{0};
  uint32_t giDiffuse{0};
  uint32_t giSpecular{0};
  uint32_t gtaoOutput{0};
  uint32_t rtReflections{0};
  uint32_t vsmAtlas{0};
  uint32_t vsmPageTable{0};
  uint32_t portalUbos{0};
  uint32_t volumetric{0};
  uint32_t defaultSampler{0};
  uint32_t sceneAddressesBdaLo{0};
  uint32_t sceneAddressesBdaHi{0};
};
static_assert(sizeof(LightingHeapPC) == 108);
static_assert(offsetof(LightingHeapPC, sceneAddressesBdaLo) == 100);

struct PostProcessHeapPC {
  uint32_t globalUbo{0};
  uint32_t hdrOutput{0};
  uint32_t hdrInput{0};
  uint32_t depthInput{0};
  uint32_t historyInput{0};
  uint32_t historyOutput{0};
  uint32_t exposureBuffer{0};
  uint32_t dirtInput{0};
  uint32_t normalInput{0};
  uint32_t halftoneInput{0};
  uint32_t paletteInput{0};
  uint32_t ditherInput{0};
  uint32_t defaultSampler{0};
};
static_assert(sizeof(PostProcessHeapPC) == 52);

/** Must match shaders/mat_preview.comp.slang MatPreviewPC */
struct MatPreviewPC {
  float4 albedoColor{};
  float4 emissiveColor{};
  float4 matParams{};
  float4 uvScale_triSc{};
  float4 camPos_time{};
  int32_t flags[4]{};
  uint32_t outImage{0};
  uint32_t texAlbedo{0};
  uint32_t texORM{0};
  uint32_t texNormal{0};
  uint32_t texHDR{0};
  uint32_t defaultSampler{0};
};

struct VolumetricHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferDepth{0};
  uint32_t gbufferPortalId{0};
  uint32_t shadowCsm{0};
  uint32_t shadowAtlas{0};
  uint32_t blueNoise{0};
  uint32_t lightBuffer{0};
  uint32_t lightGrid{0};
  uint32_t lightIndexList{0};
  uint32_t faceMatrices{0};
  uint32_t vsmAtlas{0};
  uint32_t vsmPageTable{0};
  uint32_t volumetricOut{0};
  uint32_t portalUbos{0};
  uint32_t defaultSampler{0};
};

struct RtReflectionHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferNormal{0};
  uint32_t gbufferDepth{0};
  uint32_t rtTlas{0};
  uint32_t objectStorage{0};
  uint32_t materialStorage{0};
  uint32_t giDiffuse{0};
  uint32_t rtReflections{0};
  uint32_t defaultSampler{0};
};

struct GtaoHeapPC {
  uint32_t globalUbo{0};
  uint32_t depthTex{0};
  uint32_t normalTex{0};
  uint32_t gtaoOutput{0};
  uint32_t defaultSampler{0};
};

struct SsgiHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferNormal{0};
  uint32_t gbufferDepth{0};
  uint32_t hdrOutput{0};
  uint32_t ssgiRaw{0};
  uint32_t blueNoise{0};
  uint32_t defaultSampler{0};
};

struct SsgiDenoiseHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferNormal{0};
  uint32_t gbufferDepth{0};
  uint32_t gbufferAlbedo{0};
  uint32_t hdrOutput{0};
  uint32_t ssgiRaw{0};
  uint32_t giDiffuse{0};
  uint32_t defaultSampler{0};
};

struct HiZHeapPC {
  uint32_t depthIn{0};
  uint32_t hiZOut{0};
  float2 invSize{};
  uint32_t defaultSampler{0};
};

struct LightCullHeapPC {
  uint32_t globalUbo{0};
  uint32_t lightBuffer{0};
  uint32_t lightGrid{0};
  uint32_t lightIndexList{0};
};

struct VsmMarkHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferDepth{0};
  uint32_t lightBuffer{0};
  uint32_t lightGrid{0};
  uint32_t lightIndexList{0};
  uint32_t faceMatrices{0};
  uint32_t vsmAllocator{0};
};

struct CascadeMergePC {
  uint32_t currentCascade{0};
  uint32_t nextCascade{0};
  float4 probeGridMin{};
  float4 probeGridMax{};
  int32_t probeCount[4]{};
  float4 params{};
};

struct GiSampleHeapPC {
  uint32_t globalUbo{0};
  uint32_t gbufferNormal{0};
  uint32_t gbufferDepth{0};
  uint32_t giCascade0{0};
  uint32_t giDiffuseOut{0};
  uint32_t giSpecularOut{0};
  float4 probeGridMin{};
  float4 probeGridMax{};
  int32_t probeCount[4]{};
  float4 params{};
};

struct ProbeRenderHeapPC {
  uint32_t globalUbo{0};
  uint32_t giCascade0{0};
  uint32_t outProbeTex{0};
  uint32_t rtTlas{0};
  uint32_t objectStorage{0};
  uint32_t materialStorage{0};
  uint32_t defaultSampler{0};
  float4 probePos{};
  int32_t resolution{0};
};

// Legacy aliases
using GfxHeapPush = GfxHeapPC;
using ShadowGfxPush = ShadowVertPC;
using PortalGfxPush = PortalVertPC;
using LightingPushConstants = LightingHeapPC;
using PostProcessPush = PostProcessHeapPC;
using VolumetricPush = VolumetricHeapPC;
using RtReflectionPush = RtReflectionHeapPC;
using GtaoPushConstants = GtaoHeapPC;
using SsgiPushConstants = SsgiHeapPC;

inline GfxHeapPC makeGfxPush(const FrameBindSlots& s, uint32_t frameIndex, const BurnhopeDevice* /*device*/ = nullptr) {
  GfxHeapPC p{};
  (void)s;
  (void)frameIndex;
  p.gfxDebug = packGfxDebugFlags();
  return p;
}

inline GfxHeapPC makeGfxPushForDraw(
    const FrameBindSlots& s,
    uint32_t frameIndex,
    const BurnhopeDevice& device,
    const BurnhopeBuffer* objectBuffer,
    uint32_t subMeshCount) {
  GfxHeapPC p = makeGfxPush(s, frameIndex, &device);
  p.heapResourceBase = 0u;
  if (objectBuffer != nullptr && subMeshCount > 0) {
    const void* mapped = objectBuffer->getMappedMemory();
    if (mapped != nullptr) {
      const ObjectData* objs = static_cast<const ObjectData*>(mapped);
      const ObjectData& o = objs[0];
      p.cpuLodTriCount = o.indexCount / 3u;
      p.meshVertexCount = o.vertexCount;
      p.meshIndexBase = o.pad0;
    }
  }
  return p;
}

inline PostProcessHeapPC makePostPush(const FrameBindSlots& s, uint32_t frameIndex) {
  PostProcessHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.hdrInput = s.hdrOutputSampled ? s.hdrOutputSampled : s.hdrOutput;
  p.hdrOutput = s.postProcess;
  p.depthInput = s.gbufferDepth;
  p.historyInput = s.taaHistory;
  p.historyOutput = s.taaHistory;
  p.exposureBuffer = s.exposureBuffer;
  p.normalInput = s.gbufferNormal;
  p.dirtInput = s.blueNoise;
  p.halftoneInput = s.blueNoise;
  p.paletteInput = s.blueNoise;
  p.ditherInput = s.blueNoise;
  p.defaultSampler = s.defaultSampler;
  return p;
}

inline VolumetricHeapPC makeVolumetricPush(const FrameBindSlots& s, uint32_t frameIndex) {
  VolumetricHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.gbufferDepth = s.gbufferDepth;
  p.gbufferPortalId = s.gbufferPortalId;
  p.shadowCsm = s.shadowCsm;
  p.shadowAtlas = s.shadowAtlas;
  p.blueNoise = s.blueNoise;
  p.lightBuffer = s.lightBuffer;
  p.lightGrid = s.lightGrid;
  p.lightIndexList = s.lightIndexList;
  p.faceMatrices = s.faceMatrices;
  p.vsmAtlas = s.vsmAtlas;
  p.vsmPageTable = s.vsmPageTable;
  p.volumetricOut = s.volumetricStorage ? s.volumetricStorage : s.volumetric;
  p.portalUbos = s.portalUbos;
  p.defaultSampler = s.defaultSampler;
  return p;
}

inline RtReflectionHeapPC makeRtPush(const FrameBindSlots& s, uint32_t frameIndex) {
  RtReflectionHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.gbufferNormal = s.gbufferNormal;
  p.gbufferDepth = s.gbufferDepth;
  p.rtTlas = s.rtTlas;
  p.objectStorage = s.objectStorage;
  p.materialStorage = s.materialStorage;
  p.giDiffuse = s.giDiffuse;
  p.rtReflections = s.rtReflections;
  p.defaultSampler = s.defaultSampler;
  return p;
}

/** Fill missing optional heap slots so lighting.comp can run on minimal path. */
inline void fillLightingPushFallbacks(LightingHeapPC& p) noexcept {
  const uint32_t stub = p.hdrOutput != 0u ? p.hdrOutput : p.gbufferAlbedo;
  if (p.giDiffuse == 0u) {
    p.giDiffuse = stub;
  }
  if (p.giSpecular == 0u) {
    p.giSpecular = stub;
  }
  if (p.gtaoOutput == 0u) {
    p.gtaoOutput = stub;
  }
  if (p.rtReflections == 0u) {
    p.rtReflections = stub;
  }
  if (p.volumetric == 0u) {
    p.volumetric = stub;
  }
  if (p.blueNoise == 0u) {
    p.blueNoise = p.gbufferAlbedo;
  }
}

inline LightingHeapPC makeLightingPush(const FrameBindSlots& s, uint32_t frameIndex,
                                       uint64_t sceneAddressesBda = 0) {
  LightingHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.sceneAddressesBdaLo = static_cast<uint32_t>(sceneAddressesBda & 0xFFFFFFFFu);
  p.sceneAddressesBdaHi = static_cast<uint32_t>(sceneAddressesBda >> 32);
  p.gbufferNormal = s.gbufferNormal;
  p.gbufferAlbedo = s.gbufferAlbedo;
  p.gbufferHeightAo = s.gbufferHeightAo;
  p.gbufferDepth = s.gbufferDepth;
  p.gbufferEmissive = s.gbufferEmissive;
  p.gbufferPortalId = s.gbufferPortalId;
  p.shadowCsm = s.shadowCsm;
  p.shadowAtlas = s.shadowAtlas;
  p.blueNoise = s.blueNoise;
  p.lightBuffer = s.lightBuffer;
  p.lightGrid = s.lightGrid;
  p.lightIndexList = s.lightIndexList;
  p.faceMatrices = s.faceMatrices;
  p.decalBuffer = s.decalBuffer;
  p.hdrOutput = s.hdrOutput;
  p.giDiffuse = s.giDiffuse;
  p.giSpecular = s.giSpecular;
  p.gtaoOutput = s.gtaoOutput;
  p.rtReflections = s.rtReflections;
  p.vsmAtlas = s.vsmAtlas;
  p.vsmPageTable = s.vsmPageTable;
  p.portalUbos = s.portalUbos;
  p.volumetric = s.volumetric;
  p.defaultSampler = s.defaultSampler;
  fillLightingPushFallbacks(p);
  return p;
}

inline GtaoHeapPC makeGtaoPush(const FrameBindSlots& s, uint32_t frameIndex) {
  GtaoHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.depthTex = s.gbufferDepth;
  p.normalTex = s.gbufferNormal;
  p.gtaoOutput = s.gtaoOutput;
  p.defaultSampler = s.defaultSampler;
  return p;
}

inline SsgiHeapPC makeSsgiPush(const FrameBindSlots& s, uint32_t frameIndex) {
  SsgiHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.gbufferNormal = s.gbufferNormal;
  p.gbufferDepth = s.gbufferDepth;
  p.hdrOutput = s.hdrOutput;
  p.ssgiRaw = s.ssgiRaw;
  p.blueNoise = s.blueNoise;
  p.defaultSampler = s.defaultSampler;
  return p;
}

inline SsgiDenoiseHeapPC makeSsgiDenoisePush(const FrameBindSlots& s, uint32_t frameIndex) {
  SsgiDenoiseHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.gbufferNormal = s.gbufferNormal;
  p.gbufferDepth = s.gbufferDepth;
  p.gbufferAlbedo = s.gbufferAlbedo;
  p.hdrOutput = s.hdrOutput;
  p.ssgiRaw = s.ssgiRaw;
  p.giDiffuse = s.giDiffuse;
  p.defaultSampler = s.defaultSampler;
  return p;
}

inline LightCullHeapPC makeLightCullPush(const FrameBindSlots& s, uint32_t frameIndex) {
  LightCullHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.lightBuffer = s.lightBuffer;
  p.lightGrid = s.lightGrid;
  p.lightIndexList = s.lightIndexList;
  return p;
}

inline VsmMarkHeapPC makeVsmMarkPush(const FrameBindSlots& s, uint32_t frameIndex) {
  VsmMarkHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.gbufferDepth = s.gbufferDepth;
  p.lightBuffer = s.lightBuffer;
  p.lightGrid = s.lightGrid;
  p.lightIndexList = s.lightIndexList;
  p.faceMatrices = s.faceMatrices;
  p.vsmAllocator = s.vsmAllocator;
  return p;
}

inline GiSampleHeapPC makeGiSamplePush(
    const FrameBindSlots& s,
    uint32_t frameIndex,
    float4 probeGridMin,
    float4 probeGridMax,
    int32_t probeX,
    int32_t probeY,
    int32_t probeZ,
    int32_t octaSize) {
  GiSampleHeapPC p{};
  p.globalUbo = s.globalUbo[frameIndex % 3];
  p.gbufferNormal = s.gbufferNormal;
  p.gbufferDepth = s.gbufferDepth;
  p.giCascade0 = s.giCascade0 ? s.giCascade0 : s.giDiffuse;
  p.giDiffuseOut = s.giDiffuse;
  p.giSpecularOut = s.giSpecular ? s.giSpecular : s.giDiffuse;
  p.probeGridMin = probeGridMin;
  p.probeGridMax = probeGridMax;
  p.probeCount[0] = probeX;
  p.probeCount[1] = probeY;
  p.probeCount[2] = probeZ;
  p.probeCount[3] = 0;
  p.params = {0.f, 0.f, 0.f, static_cast<float>(octaSize)};
  return p;
}

inline CascadeMergePC makeCascadeMergePush(
    const FrameBindSlots& s,
    float4 probeGridMin,
    float4 probeGridMax,
    int32_t probeX,
    int32_t probeY,
    int32_t probeZ,
    int32_t octaSize) {
  CascadeMergePC p{};
  p.currentCascade = s.giCascade0 ? s.giCascade0 : s.giDiffuse;
  p.nextCascade = s.giDiffuse;
  p.probeGridMin = probeGridMin;
  p.probeGridMax = probeGridMax;
  p.probeCount[0] = probeX;
  p.probeCount[1] = probeY;
  p.probeCount[2] = probeZ;
  p.probeCount[3] = 0;
  p.params = {0.f, 0.f, 0.f, static_cast<float>(octaSize)};
  return p;
}

} // namespace burnhope
