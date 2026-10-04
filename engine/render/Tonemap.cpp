#include "render/Tonemap.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapMaps.hpp"
#include "render/PassCommon.hpp"
#include "render/PassRendering.hpp"
#include "render/PassShaderGroup.hpp"
#include "render/PassTransitions.hpp"
#include "rhi/MeshDraw.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace burnhope {

bool tonemapPassCreate(TonemapPass& p, Device& d, const DescriptorHeaps& heaps) {
    tonemapPassDestroy(p, d);
    VkDescriptorSetAndBindingMappingEXT maps[6];
    heapMapsTonemap(heaps, maps);
    const ShaderCreateDesc mesh{
        .path = BH_SHADER_DIR "/tonemap.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 2,
        .mappings = maps,
    };
    const ShaderCreateDesc frag{
        .path = BH_SHADER_DIR "/tonemap.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 6,
        .mappings = maps,
    };
    VkDescriptorSetAndBindingMappingEXT bloomMaps[4];
    heapMapsBloom(heaps, bloomMaps);
    const char* downPath[4] = {
        BH_SHADER_DIR "/bloom0.comp.spv",
        BH_SHADER_DIR "/bloom1.comp.spv",
        BH_SHADER_DIR "/bloom2.comp.spv",
        BH_SHADER_DIR "/bloom3.comp.spv",
    };
    const char* upPath[3] = {
        BH_SHADER_DIR "/bloomup0.comp.spv",
        BH_SHADER_DIR "/bloomup1.comp.spv",
        BH_SHADER_DIR "/bloomup2.comp.spv",
    };
    const uint32_t downCount[4] = {4u, 3u, 3u, 3u};
    bool bloomOk = shaderCreate(d, mesh, p.mesh) && shaderCreate(d, frag, p.frag)
        && createShaderArray(d, p.down, downPath, VK_SHADER_STAGE_COMPUTE_BIT, bloomMaps, downCount)
        && createShaderArray(d, p.up, upPath, VK_SHADER_STAGE_COMPUTE_BIT, bloomMaps, 3u);
    VkDescriptorSetAndBindingMappingEXT cmaaMaps[4];
    heapMapsCmaa(heaps, cmaaMaps);
    const ShaderCreateDesc cmaa{
        .path = BH_SHADER_DIR "/cmaa2.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 4,
        .mappings = cmaaMaps,
    };
    if (!bloomOk || !shaderCreate(d, cmaa, p.cmaa)) {
        spdlog::error("tonemap shaders failed");
        tonemapPassDestroy(p, d);
        return false;
    }
    return true;
}

void tonemapPassDestroy(TonemapPass& p, Device& d) {
    shaderDestroy(d, p.mesh);
    shaderDestroy(d, p.frag);
    destroyShaderArray(d, p.down);
    destroyShaderArray(d, p.up);
    shaderDestroy(d, p.cmaa);
    gpuImageDestroy(p.bloom, d);
}

void cmaaPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const TonemapPass& p) {
        dispatchCompute2D(cmd, p.cmaa, extent);
}

void bloomPassRecord(VkCommandBuffer cmd, const Swapchain& sc, const TonemapPass& p, bool run) {
    transitionHdrToStorageWrite(cmd, p.bloom.image);
    if (!run) {
        transitionHdrStorageToSampled(cmd, p.bloom.image, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return;
    }
    for (uint32_t level = 0; level < 4; ++level) {
        const VkExtent2D band = mipExtent(sc.extent, level + 1u);
        dispatchCompute2D(cmd, p.down[level], band);
        transitionHdrStorageToSampled(cmd, p.bloom.image,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    }
    for (int level = 2; level >= 0; --level) {
        const VkExtent2D band = mipExtent(sc.extent, static_cast<uint32_t>(level) + 1u);
        dispatchCompute2D(cmd, p.up[static_cast<uint32_t>(level)], band);
        transitionHdrStorageToSampled(cmd, p.bloom.image,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    }
    transitionHdrStorageToSampled(cmd, p.bloom.image, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void tonemapDrawRecord(VkCommandBuffer cmd, const Swapchain& sc, const FrameContext& fc, const TonemapPass& p) {
    if (fc.imageIndex >= 8 || sc.inColor[fc.imageIndex] == 0) {
        transitionToColorAttachment(cmd, sc.images[fc.imageIndex]);
    }

    VkClearValue clear{};
    clear.color.float32[3] = 1.0f;
    RenderingPass rendering = beginRenderingColorDepth(cmd, sc.extent, sc.views[fc.imageIndex], VK_NULL_HANDLE, clear, {}, true);
    cmdSetGraphicsDynamic(cmd, sc.extent, 1, false);
    meshDrawRecord(cmd, MeshDrawDesc{.mesh = p.mesh.handle, .frag = p.frag.handle, .groupCount = 1});
    endRendering(rendering);
}

void tonemapPassRecord(
    VkCommandBuffer cmd,
    const Swapchain& sc,
    const FrameContext& fc,
    const TonemapPass& p) {
    bloomPassRecord(cmd, sc, p, true);
    tonemapDrawRecord(cmd, sc, fc, p);
}

} // namespace burnhope
