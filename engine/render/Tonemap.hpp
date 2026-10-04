#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"
#include "rhi/Swapchain.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct TonemapPass {
    ShaderExt mesh;
    ShaderExt frag;
    ShaderExt down[4];
    ShaderExt up[3];
    ShaderExt cmaa;
    GpuImage bloom;
};

[[nodiscard]] bool tonemapPassCreate(TonemapPass& p, Device& d, const DescriptorHeaps& heaps);
void tonemapPassDestroy(TonemapPass& p, Device& d);
void cmaaPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const TonemapPass& p);
void bloomPassRecord(VkCommandBuffer cmd, const Swapchain& sc, const TonemapPass& p, bool run);
void tonemapDrawRecord(VkCommandBuffer cmd, const Swapchain& sc, const FrameContext& fc, const TonemapPass& p);
void tonemapPassRecord(
    VkCommandBuffer cmd,
    const Swapchain& sc,
    const FrameContext& fc,
    const TonemapPass& p);

} // namespace burnhope
