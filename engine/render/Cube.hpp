#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct CubePass {
    ShaderExt mesh[6];
    ShaderExt frag;
    ShaderExt bake;
};

[[nodiscard]] bool cubePassCreate(CubePass& p, Device& d, const DescriptorHeaps& heaps);
void cubePassDestroy(CubePass& p, Device& d);
void cubePassRecord(VkCommandBuffer cmd, const CubePass& p, TrackedImage& color, TrackedImage& depth, const uint32_t counts[6], uint32_t face, bool clearColor);
void probeBakeRecord(VkCommandBuffer cmd, const CubePass& p, VkBuffer probes, VkDeviceSize probeBytes);

} // namespace burnhope
