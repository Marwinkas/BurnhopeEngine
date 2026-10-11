#include "render/Tonemap.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/PassRendering.hpp"
#include "render/PassShaderGroup.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/MeshDraw.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace burnhope {

bool tonemapPassCreate(TonemapPass& p, Device& d, const DescriptorHeaps& heaps) {
    tonemapPassDestroy(p, d);
    PassBindings tone{heaps};
    tone.sampledImg(6, HeapImg::HdrASampled);
    tone.sampler(1, HeapSamp::Linear, 1);
    tone.ubo(0, HeapBuf::Frame);
    tone.sampledImg(33, HeapImg::Bloom);
    tone.knob(50, HeapBuf::Flags);
    tone.knob(57, HeapBuf::Bloom);
    VkDescriptorSetAndBindingMappingEXT maps[6];
    for (uint32_t i = 0; i < tone.raw.count; ++i) {
        maps[i] = tone.raw.mappings[i];
    }
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
    PassBindings bloom{heaps};
    bloom.ubo(0, HeapBuf::Frame);
    bloom.sampledImg(6, HeapImg::HdrBSampled);
    bloom.storageImg(33, HeapImg::BloomStorage);
    bloom.knob(57, HeapBuf::Bloom);
    VkDescriptorSetAndBindingMappingEXT bloomMaps[4];
    for (uint32_t i = 0; i < bloom.raw.count; ++i) {
        bloomMaps[i] = bloom.raw.mappings[i];
    }
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
    PassBindings cmaaBind{heaps};
    cmaaBind.ubo(0, HeapBuf::Frame);
    cmaaBind.sampledImg(6, HeapImg::HdrBSampled);
    cmaaBind.storageImg(3, HeapImg::HdrAStorage);
    cmaaBind.knob(50, HeapBuf::Flags);
    VkDescriptorSetAndBindingMappingEXT cmaaMaps[4];
    for (uint32_t i = 0; i < cmaaBind.raw.count; ++i) {
        cmaaMaps[i] = cmaaBind.raw.mappings[i];
    }
    const ShaderCreateDesc cmaa{
        .path = BH_SHADER_DIR "/cmaa2.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 4,
        .mappings = cmaaMaps,
    };
    PassBindings smaaBind{heaps};
    smaaBind.ubo(0, HeapBuf::Frame);
    smaaBind.storageImg(3, HeapImg::HdrAStorage);
    smaaBind.storageImg(4, HeapImg::HdrBStorage);
    smaaBind.sampledImg(6, HeapImg::HdrBSampled);
    smaaBind.knob(50, HeapBuf::Flags);
    VkDescriptorSetAndBindingMappingEXT smaaMaps[5];
    for (uint32_t i = 0; i < smaaBind.raw.count; ++i) {
        smaaMaps[i] = smaaBind.raw.mappings[i];
    }
    const ShaderCreateDesc smaa{
        .path = BH_SHADER_DIR "/smaa.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 5,
        .mappings = smaaMaps,
    };
    if (!bloomOk || !shaderCreate(d, cmaa, p.cmaa) || !shaderCreate(d, smaa, p.smaa)) {
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
    shaderDestroy(d, p.smaa);
    shaderDestroy(d, p.cmaa);
    destroyTracked(d, {&p.bloom});
}

void cmaaPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const TonemapPass& p) {
    dispatchCompute2D(cmd, p.cmaa, extent);
}

void smaaPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const TonemapPass& p) {
    dispatchCompute2D(cmd, p.smaa, extent);
}

void bloomPassRecord(VkCommandBuffer cmd, const Swapchain& sc, TonemapPass& p, bool run) {
    if (!run) {
        imageBarrier(cmd, p.bloom, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        return;
    }
    dispatchMipChainDown(cmd, p.down, 4, sc.extent, p.bloom, 1);
    dispatchMipChainUp(cmd, p.up, 3, sc.extent, p.bloom, 1);
    imageBarrier(cmd, p.bloom, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void tonemapDrawRecord(VkCommandBuffer cmd, const Swapchain& sc, const FrameContext& fc, const TonemapPass& p) {
    if (fc.imageIndex >= 8 || sc.inColor[fc.imageIndex] == 0) {
        TrackedImage pic{};
        pic.image.image = sc.images[fc.imageIndex];
        imageBarrier(cmd, pic, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    VkClearValue clear{};
    clear.color.float32[3] = 1.0f;
    RenderingPass rendering = beginRenderingColorDepth(cmd, sc.extent, sc.views[fc.imageIndex], VK_NULL_HANDLE, clear, {}, true);
    cmdSetGraphicsDynamic(cmd, sc.extent, 1, false);
    meshDrawRecord(cmd, MeshDrawDesc{.mesh = p.mesh.handle, .frag = p.frag.handle, .groupCount = 1});
    endRendering(rendering);
}

} // namespace burnhope
