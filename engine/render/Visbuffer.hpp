#pragma once

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
    ShaderExt mesh;
    ShaderExt frag;
};

[[nodiscard]] bool visPassCreate(VisPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void visPassDestroy(VisPass& p, Device& d);
[[nodiscard]] bool visPassResize(VisPass& p, Device& d, VkExtent2D extent);

void visPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const VisPass& p);

} // namespace burnhope
