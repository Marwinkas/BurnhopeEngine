#include "CullingSystem.hpp"

namespace burnhope {

CullingSystem::CullingSystem(BurnhopeDevice& device, BindlessRegistry& bindless, uint32_t maxObjects)
    : device_{device}, bindless_{bindless}, maxObjects_{maxObjects} {
  subMeshBuffer_ = std::make_unique<BurnhopeBuffer>(
      device_,
      sizeof(SubMeshGPUInfo),
      maxObjects,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  subMeshBuffer_->map();

  drawCommandBuffer_ = std::make_unique<BurnhopeBuffer>(
      device_,
      sizeof(uint32_t) * 5,
      maxObjects,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  cullShader_ = std::make_unique<ComputeDispatch>(
      device_, "shaders/culling.comp.spv", static_cast<uint32_t>(sizeof(CullPushConstants)));
}

CullingSystem::~CullingSystem() = default;

void CullingSystem::bindObjectBuffer(VkBuffer objectBuf, VkDeviceSize objectBufSize) {
  objectHeap_ = bindless_.registerStorageBuffer(objectBuf, 0, objectBufSize);
  subMeshHeap_ = bindless_.registerStorageBuffer(
      subMeshBuffer_->handle(), 0, subMeshBuffer_->bufferSize());
  drawCmdHeap_ = bindless_.registerStorageBuffer(
      drawCommandBuffer_->handle(), 0, drawCommandBuffer_->bufferSize());
}

void CullingSystem::uploadSubMeshData(const std::vector<SubMeshGPUInfo>& subMeshes) {
  subMeshBuffer_->write(
      {reinterpret_cast<const std::byte*>(subMeshes.data()),
       subMeshes.size() * sizeof(SubMeshGPUInfo)});
}

void CullingSystem::updateHiZHeapIndex(uint32_t hiZHeapIndex) { hiZHeapIndex_ = hiZHeapIndex; }

void CullingSystem::dispatchCulling(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const float4x4& viewProj,
    const float3& camPos,
    const std::array<float4, 6>& frustumPlanes,
    uint32_t objectCount) {
  CullPushConstants pc{};
  pc.objectHeap = objectHeap_;
  pc.subMeshHeap = subMeshHeap_;
  pc.drawCmdHeap = drawCmdHeap_;
  pc.hiZHeap = hiZHeapIndex_;
  pc.defaultSampler = bindless.slots().defaultSampler;
  pc.viewProj = viewProj;
  for (int i = 0; i < 6; ++i) {
    pc.frustumPlanes[i] = frustumPlanes[static_cast<std::size_t>(i)];
  }
  pc.camPos = camPos;
  pc.objectCount = objectCount;
  pc.zNear = 0.1f;

  cullShader_->bind(cmd);
  cullShader_->pushConstants(cmd, bindless, &pc, sizeof(pc));
  cullShader_->dispatch(cmd, (objectCount + 63) / 64, 1, 1);
}

VkBuffer CullingSystem::drawCommandBuffer() const noexcept {
  return drawCommandBuffer_->handle();
}

std::array<float4, 6> CullingSystem::extractFrustumPlanes(const float4x4& vp) {
  std::array<float4, 6> planes{};
  planes[0] = float4{vp._14 + vp._11, vp._24 + vp._21, vp._34 + vp._31, vp._44 + vp._41};
  planes[1] = float4{vp._14 - vp._11, vp._24 - vp._21, vp._34 - vp._31, vp._44 - vp._41};
  planes[2] = float4{vp._14 + vp._12, vp._24 + vp._22, vp._34 + vp._32, vp._44 + vp._42};
  planes[3] = float4{vp._14 - vp._12, vp._24 - vp._22, vp._34 - vp._32, vp._44 - vp._42};
  planes[4] = float4{vp._14 + vp._13, vp._24 + vp._23, vp._34 + vp._33, vp._44 + vp._43};
  planes[5] = float4{vp._14 - vp._13, vp._24 - vp._23, vp._34 - vp._33, vp._44 - vp._43};
  for (auto& p : planes) {
    const float len = Length(float3{p.x, p.y, p.z});
    if (len > 0.0f) {
      p.x /= len;
      p.y /= len;
      p.z /= len;
      p.w /= len;
    }
  }
  return planes;
}

} // namespace burnhope
