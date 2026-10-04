#pragma once

#include "render/PassTargets.hpp"
#include "rhi/Device.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct SsrPass {
    ShaderExt cs[2];
    ShaderExt up[2];
    ShaderExt hiz[2][6];
    ShaderExt hizMax[2][6];
    GpuImage half;
};

[[nodiscard]] bool ssrPassCreate(SsrPass& p, Device& d, const DescriptorHeaps& heaps);
void ssrPassDestroy(SsrPass& p, Device& d);
void hizBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, TrackedImage& hiz, uint32_t flight);
void hizMaxBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, TrackedImage& hiz, uint32_t flight);
void ssrTraceRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, uint32_t flight);
void ssrUpRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, uint32_t flight);
void ssrPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, TrackedImage& hiz, uint32_t flight);

} // namespace burnhope
