#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct CubePass {
    ShaderExt mesh[6];
    ShaderExt frag;
};

[[nodiscard]] bool cubePassCreate(CubePass& p, Device& d, const DescriptorHeaps& heaps);
void cubePassDestroy(CubePass& p, Device& d);
void cubePassRecord(VkCommandBuffer cmd, const CubePass& p, const GpuImage& color, const GpuImage& depth, const uint32_t counts[6]);

} // namespace burnhope
