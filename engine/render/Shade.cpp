#include "render/Shade.hpp"
#include "render/HeapMaps.hpp"
#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

bool hdrCreate(GpuImage& img, Device& d, VkExtent2D extent) {
    return gpuImageCreate(
        img, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
}

} // namespace

bool shadePassCreate(ShadePass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    shadePassDestroy(p, d);
    if (!hdrCreate(p.hdrA, d, extent) || !hdrCreate(p.hdrB, d, extent)) {
        shadePassDestroy(p, d);
        return false;
    }
    VkDescriptorSetAndBindingMappingEXT maps[6];
    heapMapsShade(heaps, maps);
    const ShaderCreateDesc desc{
        .path = "shaders/shade.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 6,
        .mappings = maps,
    };
    if (!shaderCreate(d, desc, p.cs)) {
        spdlog::error("shade compute shader failed");
        shadePassDestroy(p, d);
        return false;
    }
    return true;
}

void shadePassDestroy(ShadePass& p, Device& d) {
    shaderDestroy(d, p.cs);
    gpuImageDestroy(p.hdrB, d);
    gpuImageDestroy(p.hdrA, d);
}

bool shadePassResize(ShadePass& p, Device& d, VkExtent2D extent) {
    return hdrCreate(p.hdrA, d, extent) && hdrCreate(p.hdrB, d, extent);
}

void shadePassRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    cmdBindCompute(cmd, p.cs.handle);
    vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
}

} // namespace burnhope
