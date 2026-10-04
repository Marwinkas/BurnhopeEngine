#include "render/SSR.hpp"
#include "render/HeapMaps.hpp"
#include "render/PassCommon.hpp"
#include "render/PassShaderGroup.hpp"
#include "render/PassTransitions.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif

#include <algorithm>
#include <spdlog/spdlog.h>

namespace burnhope {

bool ssrPassCreate(SsrPass& p, Device& d, const DescriptorHeaps& heaps) {
    ssrPassDestroy(p, d);
    const char* hizPath[6] = {
        BH_SHADER_DIR "/hiz0.comp.spv",
        BH_SHADER_DIR "/hiz1.comp.spv",
        BH_SHADER_DIR "/hiz2.comp.spv",
        BH_SHADER_DIR "/hiz3.comp.spv",
        BH_SHADER_DIR "/hiz4.comp.spv",
        BH_SHADER_DIR "/hiz5.comp.spv",
    };
    const char* hizMaxPath[6] = {
        BH_SHADER_DIR "/hizmax0.comp.spv",
        BH_SHADER_DIR "/hizmax1.comp.spv",
        BH_SHADER_DIR "/hizmax2.comp.spv",
        BH_SHADER_DIR "/hizmax3.comp.spv",
        BH_SHADER_DIR "/hizmax4.comp.spv",
        BH_SHADER_DIR "/hizmax5.comp.spv",
    };
    bool ok = true;
    for (uint32_t flight = 0; ok && flight < 2; ++flight) {
        VkDescriptorSetAndBindingMappingEXT maps[14];
        VkDescriptorSetAndBindingMappingEXT upMaps[14];
        heapMapsSsr(heaps, maps, flight);
        heapMapsSsr(heaps, upMaps, flight);
        maps[13] = heapMapBlock(heaps, 52, HeapBuf::Ssr);
        upMaps[13] = heapMapBlock(heaps, 50, HeapBuf::Flags);
        const ShaderCreateDesc desc{
            .path = BH_SHADER_DIR "/ssr.comp.spv",
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .mappingCount = 14,
            .mappings = maps,
        };
        const ShaderCreateDesc up{
            .path = BH_SHADER_DIR "/ssr.up.comp.spv",
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .mappingCount = 14,
            .mappings = upMaps,
        };
        ok = shaderCreate(d, desc, p.cs[flight]) && shaderCreate(d, up, p.up[flight]);
        VkDescriptorSetAndBindingMappingEXT hizMaps[3];
        VkDescriptorSetAndBindingMappingEXT maxMaps[3];
        heapMapsHiz(heaps, hizMaps, flight);
        heapMapsHizMax(heaps, maxMaps, flight);
        ok = ok && createShaderArray(d, p.hiz[flight], hizPath, VK_SHADER_STAGE_COMPUTE_BIT, hizMaps, 3u)
            && createShaderArray(d, p.hizMax[flight], hizMaxPath, VK_SHADER_STAGE_COMPUTE_BIT, maxMaps, 3u);
    }
    if (!ok) {
        spdlog::error("ssr compute shader failed");
        ssrPassDestroy(p, d);
        return false;
    }
    return true;
}

void ssrPassDestroy(SsrPass& p, Device& d) {
    for (uint32_t flight = 0; flight < 2; ++flight) {
        shaderDestroy(d, p.cs[flight]);
        shaderDestroy(d, p.up[flight]);
        destroyShaderArray(d, p.hiz[flight]);
        destroyShaderArray(d, p.hizMax[flight]);
    }
    gpuImageDestroy(p.half, d);
}

void hizChainRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShaderExt* mips, TrackedImage& hiz) {
    const bool fresh = hiz.layout == VK_IMAGE_LAYOUT_UNDEFINED;
    transitionHdrToStorageWrite(cmd, hiz.image.image, hiz.layout,
        fresh ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        fresh ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    for (uint32_t mip = 0; mip < 6; ++mip) {
        dispatchCompute2D(cmd, mips[mip], mipExtent(extent, mip));
        transitionHdrStorageToSampled(cmd, hiz.image.image,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    }
    transitionHdrStorageToSampled(cmd, hiz.image.image, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    hiz.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void hizBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, TrackedImage& hiz, uint32_t flight) {
    hizChainRecord(cmd, extent, p.hiz[flight & 1u], hiz);
}

void hizMaxBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, TrackedImage& hiz, uint32_t flight) {
    hizChainRecord(cmd, extent, p.hizMax[flight & 1u], hiz);
}

void ssrTraceRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, uint32_t flight) {
    transitionHdrToStorageWrite(cmd, p.half.image);
    dispatchCompute2D(cmd, p.cs[flight & 1u], halfExtent(extent));
    transitionHdrStorageToSampled(cmd, p.half.image,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}

void ssrUpRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, uint32_t flight) {
    dispatchCompute2D(cmd, p.up[flight & 1u], extent);
}

void ssrPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const SsrPass& p, TrackedImage& hiz, uint32_t flight) {
    hizBuildRecord(cmd, extent, p, hiz, flight);
    ssrTraceRecord(cmd, extent, p, flight);
    ssrUpRecord(cmd, extent, p, flight);
}

} // namespace burnhope
