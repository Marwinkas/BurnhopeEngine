#pragma once

#include "rhi/Device.hpp"
#include "rhi/ShaderObject.hpp"

#include <vulkan/vulkan.h>

namespace burnhope {

struct DescriptorHeaps;
struct TrackedImage;

struct HizPass {
    ShaderExt hizMax[2][6];
};

[[nodiscard]] bool hizPassCreate(HizPass& p, Device& d, const DescriptorHeaps& heaps);
void hizPassDestroy(HizPass& p, Device& d);
void hizMaxBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const HizPass& p, TrackedImage& hiz, uint32_t flight);

} // namespace burnhope
