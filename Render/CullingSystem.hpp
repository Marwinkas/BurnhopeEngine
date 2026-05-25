#pragma once

#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Buffer.hpp"
#include "../Utils/Device.hpp"
#include "Core/ComputeDispatch.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <array>
#include <memory>

namespace burnhope {

struct SubMeshGPUInfo {
  float3 aabbMin;
  float boundingRadius;
  float3 aabbMax;
  uint32_t lodCount;
  uint32_t indexCounts[4];
  uint32_t firstIndices[4];
  uint32_t materialIndex;
  uint32_t vrsRate;
  uint32_t pad2;
  uint32_t pad3;
};

struct CullPushConstants {
  uint32_t objectHeap{0};
  uint32_t subMeshHeap{0};
  uint32_t drawCmdHeap{0};
  uint32_t hiZHeap{0};
  uint32_t defaultSampler{0};
  float4x4 viewProj{};
  float4 frustumPlanes[6]{};
  float3 camPos{};
  uint32_t objectCount{0};
  float zNear{0.1f};
};

class CullingSystem final {
public:
  CullingSystem(BurnhopeDevice& device, BindlessRegistry& bindless, uint32_t maxObjects);
  ~CullingSystem();

  void bindObjectBuffer(VkBuffer objectBuf, VkDeviceSize objectBufSize);
  void uploadSubMeshData(const std::vector<SubMeshGPUInfo>& subMeshes);
  void updateHiZHeapIndex(uint32_t hiZHeapIndex);

  void dispatchCulling(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const float4x4& viewProj,
      const float3& camPos,
      const std::array<float4, 6>& frustumPlanes,
      uint32_t objectCount);

  [[nodiscard]] VkBuffer drawCommandBuffer() const noexcept;
  [[nodiscard]] uint32_t maxDrawCount() const noexcept { return maxObjects_; }

  static std::array<float4, 6> extractFrustumPlanes(const float4x4& vp);

private:
  BurnhopeDevice& device_;
  BindlessRegistry& bindless_;
  uint32_t maxObjects_{0};
  uint32_t hiZHeapIndex_{0};
  std::unique_ptr<BurnhopeBuffer> subMeshBuffer_;
  std::unique_ptr<BurnhopeBuffer> drawCommandBuffer_;
  std::unique_ptr<ComputeDispatch> cullShader_;
  uint32_t objectHeap_{0};
  uint32_t subMeshHeap_{0};
  uint32_t drawCmdHeap_{0};
};

} // namespace burnhope
