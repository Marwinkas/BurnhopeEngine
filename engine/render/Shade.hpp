#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct ShadePass {
    GpuImage hdrA;
    GpuImage hdrB;
    ShaderExt cs;
};

[[nodiscard]] bool shadePassCreate(ShadePass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void shadePassDestroy(ShadePass& p, Device& d);
[[nodiscard]] bool shadePassResize(ShadePass& p, Device& d, VkExtent2D extent);
void shadePassRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p);

} // namespace burnhope
