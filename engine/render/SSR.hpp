#pragma once

#include "rhi/Device.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

struct SsrPass {
    ShaderExt cs;
};

[[nodiscard]] bool ssrPassCreate(SsrPass& p, Device& d, const DescriptorHeaps& heaps);
void ssrPassDestroy(SsrPass& p, Device& d);
void ssrPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p);

} // namespace burnhope
