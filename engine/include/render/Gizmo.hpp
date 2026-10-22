#pragma once

#include "gpu_scene/SceneObject.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"
#include "rhi/Swapchain.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct GizmoPass {
    ShaderExt mesh{};
    ShaderExt frag{};
    GpuBuffer buf{};
    TrackedImage depth[2]{};
};

[[nodiscard]] bool gizmoPassCreate(GizmoPass& pass, Device& device, DescriptorHeaps& heaps);
void gizmoPassDestroy(GizmoPass& pass, Device& device);
void gizmoPassRecord(VkCommandBuffer cmd, Device& device, const Swapchain& swap, const FrameContext& frame, GizmoPass& pass, const SceneActors& actors);

} // namespace burnhope
