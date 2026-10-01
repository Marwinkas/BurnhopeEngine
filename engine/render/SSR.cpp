#include "render/SSR.hpp"
#include "render/HeapMaps.hpp"
#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool ssrPassCreate(SsrPass& p, Device& d, const DescriptorHeaps& heaps) {
    ssrPassDestroy(p, d);
    VkDescriptorSetAndBindingMappingEXT maps[6];
    heapMapsSsr(heaps, maps);
    const ShaderCreateDesc desc{
        .path = "shaders/ssr.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 6,
        .mappings = maps,
    };
    if (!shaderCreate(d, desc, p.cs)) {
        spdlog::error("ssr compute shader failed");
        return false;
    }
    return true;
}

void ssrPassDestroy(SsrPass& p, Device& d) {
    shaderDestroy(d, p.cs);
}

void ssrPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p) {
    cmdBindCompute(cmd, p.cs.handle);
    vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
}

} // namespace burnhope
