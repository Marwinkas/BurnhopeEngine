#include "render/Gtao.hpp"

#include "render/HeapBind.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/RenderDefs.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

constexpr uint32_t kStride = 256u;
constexpr uint32_t kDepthS = 974u;
constexpr uint32_t kDepth0 = 975u;
constexpr uint32_t kNormU = 980u;
constexpr uint32_t kNormS = 981u;
constexpr uint32_t kAo0U = 982u;
constexpr uint32_t kAo0S = 983u;
constexpr uint32_t kAo1U = 984u;
constexpr uint32_t kAo1S = 985u;
constexpr uint32_t kEdgeU = 986u;
constexpr uint32_t kEdgeS = 987u;

uint32_t groupsOf(uint32_t size, uint32_t group) {
    return std::max((size + group - 1u) / group, 1u);
}

uint32_t legalMips(VkExtent2D extent) {
    uint32_t mips = 1u;
    uint32_t w = extent.width;
    uint32_t h = extent.height;
    while (mips < 5u && (w > 1u || h > 1u)) {
        w = std::max(w / 2u, 1u);
        h = std::max(h / 2u, 1u);
        mips += 1u;
    }
    return mips;
}

bool makeImage(TrackedImage& img, Device& d, VkExtent2D extent, VkFormat format, uint32_t mips) {
    const bool ok = gpuImageCreate(
        img.image, d, extent, format,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mips);
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img.stage = VK_PIPELINE_STAGE_2_NONE;
    img.access = VK_ACCESS_2_NONE;
    return ok;
}

void destroyImages(GtaoPass& p, Device& d) {
    gpuImageDestroy(p.edges.image, d);
    gpuImageDestroy(p.aoTerm[1].image, d);
    gpuImageDestroy(p.aoTerm[0].image, d);
    gpuImageDestroy(p.normalsImg.image, d);
    gpuImageDestroy(p.depthMips.image, d);
    p.edges.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.aoTerm[0].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.aoTerm[1].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.normalsImg.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.depthMips.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.mips = 0;
}

bool viewSlot(DescriptorHeaps& heaps, Device& d, uint32_t slot, const GpuImage& img, VkDescriptorType type, uint32_t mips, uint32_t baseMip) {
    return heapWriteImage(heaps, d, slot, type, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, mips, baseMip);
}

bool bindImages(const GtaoPass& p, DescriptorHeaps& heaps, Device& d) {
    if (p.mips < 5u || p.depthMips.image.image == VK_NULL_HANDLE) {
        return false;
    }
    bool ok = viewSlot(heaps, d, kDepthS, p.depthMips.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5, 0);
    for (uint32_t mip = 0; ok && mip < 5u; ++mip) {
        ok = viewSlot(heaps, d, kDepth0 + mip, p.depthMips.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, mip);
    }
    return ok
        && viewSlot(heaps, d, kNormU, p.normalsImg.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewSlot(heaps, d, kNormS, p.normalsImg.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewSlot(heaps, d, kAo0U, p.aoTerm[0].image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewSlot(heaps, d, kAo0S, p.aoTerm[0].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewSlot(heaps, d, kAo1U, p.aoTerm[1].image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewSlot(heaps, d, kAo1S, p.aoTerm[1].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewSlot(heaps, d, kEdgeU, p.edges.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewSlot(heaps, d, kEdgeS, p.edges.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0);
}

bool bindConstants(const GtaoPass& p, DescriptorHeaps& heaps, Device& d) {
    bool ok = p.constants.address != 0;
    for (uint32_t flight = 0; ok && flight < 2u; ++flight) {
        ok = heapWriteBufferFlight(
            heaps, d, flight, kGtaoConstSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            p.constants.address + kStride * flight, kStride);
    }
    return ok;
}

void uboAt(PassBindings& b, uint32_t binding) {
    b.raw.push(0, binding, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(*b.raw.heaps, kGtaoConstSlot), static_cast<uint32_t>(b.raw.heaps->bufferDescSize));
}

bool makeShader(Device& d, const PassBindings& map, const char* path, ShaderExt& shader) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = map.raw.count,
        .mappings = map.raw.mappings,
    };
    return shaderCreate(d, desc, shader);
}

void commonDepth(PassBindings& map) {
    uboAt(map, 0);
    map.sampler(4, HeapSamp::Nearest);
    map.sampledImg(8, static_cast<uint32_t>(HeapImg::Depth));
}

bool createShaders(GtaoPass& p, Device& d, const DescriptorHeaps& heaps) {
    PassBindings pre{heaps};
    commonDepth(pre);
    pre.storageImg(24, kDepth0);
    pre.storageImg(25, kDepth0 + 1u);
    pre.storageImg(26, kDepth0 + 2u);
    pre.storageImg(27, kDepth0 + 3u);
    pre.storageImg(28, kDepth0 + 4u);

    PassBindings norm{heaps};
    commonDepth(norm);
    norm.storageImg(24, kNormU);

    PassBindings main{heaps};
    uboAt(main, 0);
    main.sampler(4, HeapSamp::Nearest);
    main.sampledImg(8, kDepthS);
    main.sampledImg(9, kNormS);
    main.storageImg(24, kAo0U);
    main.storageImg(25, kEdgeU);

    PassBindings denoiseA{heaps};
    uboAt(denoiseA, 0);
    denoiseA.sampler(4, HeapSamp::Nearest);
    denoiseA.sampledImg(8, kAo0S);
    denoiseA.sampledImg(9, kEdgeS);
    denoiseA.storageImg(24, kAo1U);

    PassBindings denoiseB{heaps};
    uboAt(denoiseB, 0);
    denoiseB.sampler(4, HeapSamp::Nearest);
    denoiseB.sampledImg(8, kAo1S);
    denoiseB.sampledImg(9, kEdgeS);
    denoiseB.storageImg(24, kAo0U);

    PassBindings resolveA{heaps};
    resolveA.sampledImg(8, kAo1S);
    resolveA.storageImg(24, static_cast<uint32_t>(HeapImg::AoStorage));

    PassBindings resolveB{heaps};
    resolveB.sampledImg(8, kAo0S);
    resolveB.storageImg(24, static_cast<uint32_t>(HeapImg::AoStorage));

    return makeShader(d, pre, BH_SHADER_DIR "/xegtao.prefilter.comp.spv", p.prefilter)
        && makeShader(d, norm, BH_SHADER_DIR "/xegtao.normals.comp.spv", p.normals)
        && makeShader(d, main, BH_SHADER_DIR "/xegtao.low.comp.spv", p.main[0])
        && makeShader(d, main, BH_SHADER_DIR "/xegtao.medium.comp.spv", p.main[1])
        && makeShader(d, main, BH_SHADER_DIR "/xegtao.high.comp.spv", p.main[2])
        && makeShader(d, main, BH_SHADER_DIR "/xegtao.ultra.comp.spv", p.main[3])
        && makeShader(d, denoiseA, BH_SHADER_DIR "/xegtao.denoise.comp.spv", p.denoise[0])
        && makeShader(d, denoiseB, BH_SHADER_DIR "/xegtao.denoise.comp.spv", p.denoise[1])
        && makeShader(d, denoiseA, BH_SHADER_DIR "/xegtao.denoise_last.comp.spv", p.denoiseLast[0])
        && makeShader(d, denoiseB, BH_SHADER_DIR "/xegtao.denoise_last.comp.spv", p.denoiseLast[1])
        && makeShader(d, resolveA, BH_SHADER_DIR "/xegtao.resolve.comp.spv", p.resolve[0])
        && makeShader(d, resolveB, BH_SHADER_DIR "/xegtao.resolve.comp.spv", p.resolve[1]);
}

void destroyShaders(GtaoPass& p, Device& d) {
    shaderDestroy(d, p.resolve[1]);
    shaderDestroy(d, p.resolve[0]);
    shaderDestroy(d, p.denoiseLast[1]);
    shaderDestroy(d, p.denoiseLast[0]);
    shaderDestroy(d, p.denoise[1]);
    shaderDestroy(d, p.denoise[0]);
    shaderDestroy(d, p.main[3]);
    shaderDestroy(d, p.main[2]);
    shaderDestroy(d, p.main[1]);
    shaderDestroy(d, p.main[0]);
    shaderDestroy(d, p.normals);
    shaderDestroy(d, p.prefilter);
}

void dispatch(VkCommandBuffer cmd, const ShaderExt& shader, uint32_t x, uint32_t y) {
    cmdBindCompute(cmd, shader.handle);
    vkCmdDispatch(cmd, x, y, 1u);
}

void barrierMips(VkCommandBuffer cmd, TrackedImage& img, uint32_t mips, VkAccessFlags2 access) {
    if (img.image.image == VK_NULL_HANDLE || mips == 0u) {
        return;
    }
    const bool first = img.layout == VK_IMAGE_LAYOUT_UNDEFINED;
    rhiImageBarrierMips(
        cmd, img.image.image,
        first ? VK_IMAGE_LAYOUT_UNDEFINED : img.layout,
        VK_IMAGE_LAYOUT_GENERAL,
        first ? VK_PIPELINE_STAGE_2_NONE : img.stage,
        first ? VK_ACCESS_2_NONE : img.access,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, access,
        0, mips);
    img.layout = VK_IMAGE_LAYOUT_GENERAL;
    img.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    img.access = access;
}

void clearAo(VkCommandBuffer cmd, TrackedImage& ao) {
    imageBarrier(cmd, ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue color{.float32 = {1.0f, 1.0f, 1.0f, 1.0f}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, ao.image.image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
    imageBarrier(cmd, ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

uint32_t qualityOf(float slices, float steps) {
    if (slices >= 7.0f) {
        return 3u;
    }
    if (slices >= 3.0f || steps >= 3.0f) {
        return 2u;
    }
    if (slices >= 2.0f || steps >= 2.0f) {
        return 1u;
    }
    return 0u;
}

} // namespace

bool gtaoCreate(GtaoPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent) {
    gtaoDestroy(p, d);
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (!gpuBufferCreate(p.constants, d, kStride * 2u, usage, true)) {
        gtaoDestroy(p, d);
        return false;
    }
    if (!gtaoResize(p, d, heaps, extent) || !createShaders(p, d, heaps) || !bindConstants(p, heaps, d)) {
        spdlog::error("xegtao create failed");
        gtaoDestroy(p, d);
        return false;
    }
    return true;
}

void gtaoDestroy(GtaoPass& p, Device& d) {
    destroyShaders(p, d);
    destroyImages(p, d);
    gpuBufferDestroy(p.constants, d);
    p.frame = 0;
}

bool gtaoResize(GtaoPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent) {
    destroyImages(p, d);
    if (extent.width < 16u || extent.height < 16u) {
        return true;
    }
    const uint32_t mips = legalMips(extent);
    const bool images = makeImage(p.depthMips, d, extent, VK_FORMAT_R32_SFLOAT, mips)
        && makeImage(p.normalsImg, d, extent, VK_FORMAT_R32_UINT, 1)
        && makeImage(p.aoTerm[0], d, extent, VK_FORMAT_R32_UINT, 1)
        && makeImage(p.aoTerm[1], d, extent, VK_FORMAT_R32_UINT, 1)
        && makeImage(p.edges, d, extent, VK_FORMAT_R8_UNORM, 1);
    if (!images) {
        destroyImages(p, d);
        return false;
    }
    p.mips = mips;
    if (mips < 5u) {
        return true;
    }
    return bindImages(p, heaps, d);
}

void gtaoRecord(
    VkCommandBuffer cmd,
    GtaoPass& p,
    Device& d,
    uint32_t flight,
    TrackedImage& ao,
    bool on,
    float fovY,
    float zn,
    const GtaoParams& params) {
    const VkExtent2D extent = ao.image.extent;
    const bool ready = on && p.mips >= 5u && p.prefilter.handle != VK_NULL_HANDLE
        && extent.width >= 16u && extent.height >= 16u && fovY > 0.01f && zn > 0.0f;
    if (!ready) {
        clearAo(cmd, ao);
        std::snprintf(p.lastLog, sizeof(p.lastLog), "XeGTAO clear on %u mips %u %ux%u",
            on ? 1u : 0u, p.mips, extent.width, extent.height);
        return;
    }
    const float tanY = std::tan(fovY * 0.5f);
    const float tanX = tanY * (static_cast<float>(extent.width) / static_cast<float>(extent.height));
    const int denoise = std::clamp(static_cast<int>(params.pad[0] + 0.5f), 0, 3);
    const int passes = std::max(denoise, 1);
    GtaoConstants constants{};
    constants.viewportW = static_cast<int32_t>(extent.width);
    constants.viewportH = static_cast<int32_t>(extent.height);
    constants.pixelW = 1.0f / static_cast<float>(extent.width);
    constants.pixelH = 1.0f / static_cast<float>(extent.height);
    constants.depthMul = -zn;
    constants.depthAdd = 0.0f;
    constants.tanX = tanX;
    constants.tanY = tanY;
    constants.ndcMulX = tanX * 2.0f;
    constants.ndcMulY = tanY * -2.0f;
    constants.ndcAddX = tanX * -1.0f;
    constants.ndcAddY = tanY;
    constants.ndcPxX = constants.ndcMulX * constants.pixelW;
    constants.ndcPxY = constants.ndcMulY * constants.pixelH;
    constants.radius = std::clamp(params.radius, 0.05f, 4.0f);
    constants.falloff = std::clamp(params.falloff, 0.05f, 1.0f);
    constants.radiusMul = 1.457f;
    constants.power = std::clamp(params.power, 0.5f, 4.0f);
    const float beta[] = {10000.0f, 1.2f, 0.55f, 0.22f};
    constants.denoiseBeta = beta[denoise];
    constants.samplePower = 2.0f;
    constants.thin = 0.0f;
    constants.mipOffset = 3.30f;
    constants.noise = 0;
    if (flight < 2u && p.constants.mapped != nullptr) {
        std::memcpy(static_cast<std::byte*>(p.constants.mapped) + kStride * flight, &constants, sizeof(constants));
        gpuBufferFlush(p.constants, d, kStride * flight, kStride);
        bufferBarrierHostToShader(cmd, p.constants.buffer, p.constants.size);
    }
    p.frame += 1u;

    barrierMips(cmd, p.depthMips, 5, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatch(cmd, p.prefilter, groupsOf(extent.width, 16u), groupsOf(extent.height, 16u));
    barrierMips(cmd, p.depthMips, 5, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    imageBarrier(cmd, p.normalsImg, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatch(cmd, p.normals, groupsOf(extent.width, 8u), groupsOf(extent.height, 8u));
    imageBarrier(cmd, p.normalsImg, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    imageBarrier(cmd, p.aoTerm[0], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    imageBarrier(cmd, p.edges, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const uint32_t quality = qualityOf(params.slices, params.steps);
    dispatch(cmd, p.main[quality], groupsOf(extent.width, 8u), groupsOf(extent.height, 8u));
    imageBarrier(cmd, p.aoTerm[0], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    imageBarrier(cmd, p.edges, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    for (int pass = 0; pass < passes; ++pass) {
        const bool last = pass + 1 == passes;
        const uint32_t side = static_cast<uint32_t>(pass) & 1u;
        const uint32_t dst = side ^ 1u;
        imageBarrier(cmd, p.aoTerm[dst], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
        const ShaderExt& shader = last ? p.denoiseLast[side] : p.denoise[side];
        dispatch(cmd, shader, groupsOf(extent.width, 16u), groupsOf(extent.height, 8u));
        imageBarrier(cmd, p.aoTerm[dst], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    const uint32_t finalSide = (static_cast<uint32_t>(passes) & 1u) == 1u ? 0u : 1u;
    imageBarrier(cmd, ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatch(cmd, p.resolve[finalSide], groupsOf(extent.width, 8u), groupsOf(extent.height, 8u));
    imageBarrier(cmd, ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    std::snprintf(p.lastLog, sizeof(p.lastLog), "XeGTAO %ux%u r %.2f sl %.0f st %.0f den %d q %u",
        extent.width, extent.height, static_cast<double>(constants.radius), static_cast<double>(params.slices),
        static_cast<double>(params.steps), denoise, quality);
}

} // namespace burnhope
