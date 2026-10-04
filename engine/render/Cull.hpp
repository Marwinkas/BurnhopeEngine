#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct OcclPass {
    ShaderExt clear[2]{};
    ShaderExt compact[2]{};
    ShaderExt late[2]{};
};

[[nodiscard]] bool occlPassCreate(OcclPass& p, Device& d, const DescriptorHeaps& heaps);
void occlPassDestroy(OcclPass& p, Device& d);
void occlPassRecord(VkCommandBuffer cmd, const OcclPass& p, const GpuBuffer& indirect, const GpuBuffer& flags, const GpuBuffer& visible, uint32_t count, bool late, uint32_t flight);

struct ClusterPass {
    ShaderExt cs{};
};

struct ShadowCullPass {
    ShaderExt clear{};
    ShaderExt compact{};
};

[[nodiscard]] bool clusterPassCreate(ClusterPass& p, Device& d, const DescriptorHeaps& heaps);
void clusterPassDestroy(ClusterPass& p, Device& d);
void clusterPassRecord(VkCommandBuffer cmd, const ClusterPass& p);

[[nodiscard]] bool shadowCullCreate(ShadowCullPass& p, Device& d, const DescriptorHeaps& heaps);
void shadowCullDestroy(ShadowCullPass& p, Device& d);
void shadowCullRecord(VkCommandBuffer cmd, const ShadowCullPass& p, const GpuBuffer& indirect, const GpuBuffer& lists, uint32_t count);

} // namespace burnhope
