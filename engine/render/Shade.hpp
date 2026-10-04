#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuImage.hpp"
#include "render/PassTargets.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct ShadePass {
    TrackedImage hdrA;
    TrackedImage hdrB;
    TrackedImage ao;
    TrackedImage surf0;
    TrackedImage surf1;
    ShaderExt sky;
    ShaderExt sun;
    ShaderExt punctual;
    ShaderExt fog;
    ShaderExt gtao;
};

[[nodiscard]] bool shadePassCreate(ShadePass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void shadePassDestroy(ShadePass& p, Device& d);
[[nodiscard]] bool shadePassResize(ShadePass& p, Device& d, VkExtent2D extent);
void shadeAoRecord(VkCommandBuffer cmd, VkExtent2D extent, ShadePass& p);
void shadeSkyRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p);
void shadeSunRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p);
void shadePunctualRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p);
void shadeFogRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p);

} // namespace burnhope
