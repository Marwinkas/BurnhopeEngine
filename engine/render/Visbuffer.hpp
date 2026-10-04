#pragma once

#include "render/PassTargets.hpp"
#include "rhi/Device.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct VisTargets {
    GpuImage vis;
    GpuImage depth;
};

struct VisPass {
    VisTargets targets;
    ShaderExt mesh[2];
    ShaderExt meshLate[2];
    ShaderExt shadow[3];
    ShaderExt point[3];
    ShaderExt frag;
    VkPipelineLayout pushLayout = VK_NULL_HANDLE;
};

// BDA of the instance array and of the uint visible-index list. groupCount is visibleCount.
struct VisInstanceDraw {
    uint64_t instances = 0;
    uint64_t visible = 0;
    uint32_t visibleCount = 0;
    VkBuffer indirect = VK_NULL_HANDLE;
    VkDeviceSize indirectOffset = 0;
};

[[nodiscard]] bool visPassCreate(VisPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void visPassDestroy(VisPass& p, Device& d);
[[nodiscard]] bool visPassResize(VisPass& p, Device& d, VkExtent2D extent);

void visPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const VisPass& p, const VisInstanceDraw& draw, bool load, uint32_t flight);
void visShadowRecord(VkCommandBuffer cmd, const VisPass& p, const GpuImage& depth, const VisInstanceDraw& draw, VkImageLayout depthLayout, uint32_t cascade);
void visPointRecord(VkCommandBuffer cmd, const VisPass& p, TrackedImage& depth, const uint32_t counts[3], uint64_t base, VkDeviceSize stride);

} // namespace burnhope
