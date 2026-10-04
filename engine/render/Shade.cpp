#include "render/Shade.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapMaps.hpp"
#include "render/PassTargets.hpp"
#include "render/PassCommon.hpp"
#include "render/PassTransitions.hpp"
#include "rhi/ShaderObject.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

bool storageCreate(GpuImage& img, Device& d, VkExtent2D extent, VkFormat format) {
    return recreateImage(
        img, d, extent, format,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
}

} // namespace

bool shadePassCreate(ShadePass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    shadePassDestroy(p, d);
    const bool aoOk = storageCreate(p.ao, d, extent, VK_FORMAT_R8_UNORM);
    if (!recreateHdr(p.hdrA, d, extent) || !recreateHdr(p.hdrB, d, extent) || !aoOk) {
        shadePassDestroy(p, d);
        return false;
    }
    VkDescriptorSetAndBindingMappingEXT maps[26];
    heapMapsShade(heaps, maps);
    const ShaderCreateDesc desc{
        .path = BH_SHADER_DIR "/shade.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 26,
        .mappings = maps,
    };
    VkDescriptorSetAndBindingMappingEXT gtaoMaps[5];
    heapMapsGtao(heaps, gtaoMaps);
    const ShaderCreateDesc gtao{
        .path = BH_SHADER_DIR "/gtao.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 5,
        .mappings = gtaoMaps,
    };
    if (!shaderCreate(d, desc, p.cs) || !shaderCreate(d, gtao, p.gtao)) {
        spdlog::error("shade compute shader failed");
        shadePassDestroy(p, d);
        return false;
    }
    return true;
}

void shadePassDestroy(ShadePass& p, Device& d) {
    shaderDestroy(d, p.gtao);
    shaderDestroy(d, p.cs);
    gpuImageDestroy(p.ao, d);
    gpuImageDestroy(p.hdrB, d);
    gpuImageDestroy(p.hdrA, d);
}

bool shadePassResize(ShadePass& p, Device& d, VkExtent2D extent) {
    const bool aoOk = storageCreate(p.ao, d, extent, VK_FORMAT_R8_UNORM);
    return recreateHdr(p.hdrA, d, extent) && recreateHdr(p.hdrB, d, extent) && aoOk;
}

void shadeAoRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    transitionHdrToStorageWrite(cmd, p.ao.image);
    dispatchCompute2D(cmd, p.gtao, halfExtent(extent));
    transitionHdrStorageToSampled(cmd, p.ao.image, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}

void shadeLitRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    dispatchCompute2D(cmd, p.cs, extent, 16u);
}

void shadePassRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    shadeAoRecord(cmd, extent, p);
    shadeLitRecord(cmd, extent, p);
}

} // namespace burnhope
