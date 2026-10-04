#include "render/Ssrc.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapMaps.hpp"
#include "render/PassCommon.hpp"
#include "render/PassShaderGroup.hpp"
#include "render/PassTransitions.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

bool levelCreate(TrackedImage& img, Device& d, VkExtent2D extent) {
    return recreateHdr(img.image, d, halfExtent(extent));
}

void fillMaps(const DescriptorHeaps& h, uint32_t cascade, VkDescriptorSetAndBindingMappingEXT out[17]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    const uint32_t base = static_cast<uint32_t>(HeapImg::Cascade);
    const uint32_t up = cascade < 3 ? cascade + 1 : cascade;
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT, heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT, heapImgOffset(h, static_cast<uint32_t>(HeapImg::Vis)), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT, heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[3] = heapMap(0, 3, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrAStorage)), img);
    out[4] = heapMap(0, 4, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT, heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrBSampled)), img);
    out[5] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT, heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), buf);
    out[6] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT, heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indices)), buf);
    out[7] = heapMap(0, 9, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT, heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Meshlets)), buf);
    out[8] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT, heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[9] = heapMap(0, 12, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT, heapImgOffset(h, static_cast<uint32_t>(HeapImg::Textures)), img);
    out[10] = heapMap(0, 13, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT, heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), samp);
    out[11] = heapMap(0, 14, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT, heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Materials)), buf);
    out[12] = heapMap(0, 20, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, heapImgOffset(h, base + cascade), img);
    out[13] = heapMap(0, 21, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, heapImgOffset(h, base + 4 + cascade), img);
    out[14] = heapMap(0, 22, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, heapImgOffset(h, base + up), img);
    out[15] = heapMap(0, 23, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, heapImgOffset(h, base + 4 + up), img);
    out[16] = heapMapBlock(h, 53, HeapBuf::Ssrc);
}

} // namespace

bool ssrcPassCreate(SsrcPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    ssrcPassDestroy(p, d);
    for (uint32_t set = 0; set < 2; ++set) {
        for (uint32_t c = 0; c < kSsrcCascades; ++c) {
            if (!levelCreate(p.level[set][c], d, extent)) {
                ssrcPassDestroy(p, d);
                return false;
            }
        }
    }
    const char* file[kSsrcCascades] = {
        BH_SHADER_DIR "/ssrc0.comp.spv",
        BH_SHADER_DIR "/ssrc1.comp.spv",
        BH_SHADER_DIR "/ssrc2.comp.spv",
        BH_SHADER_DIR "/ssrc3.comp.spv",
    };
    VkDescriptorSetAndBindingMappingEXT maps[kSsrcCascades][17];
    if (!createShaderGroup(d, p.trace, file, VK_SHADER_STAGE_COMPUTE_BIT, [&](uint32_t cascade, ShaderCreateDesc& desc) {
            fillMaps(heaps, cascade, maps[cascade]);
            desc.mappingCount = 17;
            desc.mappings = maps[cascade];
        })) {
        ssrcPassDestroy(p, d);
        return false;
    }
    VkDescriptorSetAndBindingMappingEXT applyMaps[17];
    fillMaps(heaps, 0, applyMaps);
    const ShaderCreateDesc apply{
        .path = BH_SHADER_DIR "/ssrc.apply.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 17,
        .mappings = applyMaps,
    };
    if (!shaderCreate(d, apply, p.apply)) {
        spdlog::error("ssrc apply shader failed");
        ssrcPassDestroy(p, d);
        return false;
    }
    p.slot = 0;
    return true;
}

void ssrcPassDestroy(SsrcPass& p, Device& d) {
    shaderDestroy(d, p.apply);
    destroyShaderArray(d, p.trace);
    for (uint32_t c = 0; c < kSsrcCascades; ++c) {
        for (uint32_t set = 0; set < 2; ++set) {
            gpuImageDestroy(p.level[set][c].image, d);
            p.level[set][c].layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }
    p.slot = 0;
}

void ssrcPassRecord(VkCommandBuffer cmd, VkExtent2D extent, SsrcPass& p) {
    const uint32_t slot = p.slot;
    const VkExtent2D half = halfExtent(extent);
    for (int c = static_cast<int>(kSsrcCascades) - 1; c >= 0; --c) {
        TrackedImage& img = p.level[slot][static_cast<uint32_t>(c)];
        const VkImageLayout old = img.layout;
        const bool first = old == VK_IMAGE_LAYOUT_UNDEFINED;
        transitionHdrToStorageWrite(cmd, img.image.image, old,
            first ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            first ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        img.layout = VK_IMAGE_LAYOUT_GENERAL;
        dispatchCompute2D(cmd, p.trace[static_cast<uint32_t>(c)], half);
        transitionHdrStorageToSampled(cmd, img.image.image, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    }
    dispatchCompute2D(cmd, p.apply, extent);
    p.slot ^= 1u;
}

} // namespace burnhope
