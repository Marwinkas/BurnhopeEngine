#pragma once

#include "rhi/Device.hpp"
#include "rhi/ShaderObject.hpp"
#include "rhi/Swapchain.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct TonemapPass {
    ShaderExt mesh;
    ShaderExt frag;
};

[[nodiscard]] bool tonemapPassCreate(TonemapPass& p, Device& d, const DescriptorHeaps& heaps);
void tonemapPassDestroy(TonemapPass& p, Device& d);
void tonemapPassRecord(
    VkCommandBuffer cmd,
    const Swapchain& sc,
    const FrameContext& fc,
    const TonemapPass& p);

} // namespace burnhope
