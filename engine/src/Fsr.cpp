#include "render/Fsr.hpp"

#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/RenderDefs.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

constexpr uint32_t kStride = 256u;

enum Slot : uint32_t {
    Motion = 0,
    DilMvS,
    DilMvU,
    DilDS,
    DilDU,
    ReconS,
    ReconU,
    FarS,
    FarU,
    FarMipS,
    FarMipU,
    LumaS,
    LumaU,
    LumaPrevS,
    SpdS,
    Spd0,
    Spd1,
    Spd2,
    Spd3,
    Spd4,
    Spd5,
    AtomicU,
    FrameS,
    FrameU,
    ShadeS,
    ShadeU,
    ReactS,
    ReactU,
    LockS,
    LockU,
    AccS,
    AccU,
    HistS,
    HistU,
    InstS,
    InstU,
    UpS,
    UpU,
    RcasS,
    RcasU,
    OutS,
    OutU,
    ExpS,
    MaskS,
    CompS,
    SlotCount,
};

struct FsrConst {
    int32_t renderSize[2];
    int32_t previousFrameRenderSize[2];
    int32_t upscaleSize[2];
    int32_t previousFrameUpscaleSize[2];
    int32_t maxRenderSize[2];
    int32_t maxUpscaleSize[2];
    float deviceToViewDepth[4];
    float jitter[2];
    float previousFrameJitter[2];
    float motionVectorScale[2];
    float downscaleFactor[2];
    float motionVectorJitterCancellation[2];
    float tanHalfFOV;
    float jitterSequenceLength;
    float deltaTime;
    float deltaPreExposure;
    float viewSpaceToMetersFactor;
    float frameIndex;
    float velocityFactor;
    float reactivenessScale;
    float shadingChangeScale;
    float accumulationAddedPerFrame;
    float minDisocclusionAccumulation;
};

struct SpdConst {
    uint32_t mips;
    uint32_t numWorkGroups;
    uint32_t workGroupOffset[2];
    uint32_t renderSize[2];
};

struct RcasConst {
    uint32_t config[4];
};

struct MotionConst {
    float currInv[16];
    float currUnjit[16];
    float prevUnjit[16];
    uint32_t width;
    uint32_t height;
    float pad[2];
};

static_assert(offsetof(FsrConst, deviceToViewDepth) == 48);
static_assert(offsetof(FsrConst, jitter) == 64);
static_assert(offsetof(FsrConst, tanHalfFOV) == 104);
static_assert(offsetof(FsrConst, minDisocclusionAccumulation) == 144);
static_assert(sizeof(FsrConst) == 148);
static_assert(sizeof(SpdConst) == 24);
static_assert(sizeof(RcasConst) == 16);
static_assert(sizeof(MotionConst) == 208);
static_assert(SlotCount + kFsrImgBase <= static_cast<uint32_t>(HeapImg::Count));

uint32_t slotOf(Slot slot) {
    return kFsrImgBase + static_cast<uint32_t>(slot);
}

uint32_t groupsOf(uint32_t n, uint32_t group) {
    return (n + group - 1u) / group;
}

uint32_t halfOf(uint32_t v) {
    return v > 1u ? v / 2u : 1u;
}

uint32_t mipCountOf(uint32_t w, uint32_t h, uint32_t cap) {
    uint32_t n = 1u;
    while (n < cap && (w > 1u || h > 1u)) {
        w = w > 1u ? w / 2u : 1u;
        h = h > 1u ? h / 2u : 1u;
        n += 1u;
    }
    return n;
}

VkExtent2D spdExtent(uint32_t renderW, uint32_t renderH) {
    uint32_t w = halfOf(renderW);
    uint32_t h = halfOf(renderH);
    while (mipCountOf(w, h, 6u) < 6u) {
        w *= 2u;
        h *= 2u;
    }
    return {w, h};
}

float halton(int32_t index, int32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

uint16_t floatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
    const uint32_t mant = bits & 0x7fffffu;
    if (exp <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10u) | (mant >> 13u));
}

void uboAt(PassBindings& bind, uint32_t binding, uint32_t slot) {
    bind.raw.push(0, binding, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(*bind.raw.heaps, slot), static_cast<uint32_t>(bind.raw.heaps->bufferDescSize));
}

bool makeShader(Device& d, PassBindings& bind, const char* path, ShaderExt& out) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
        .entry = "main",
    };
    return shaderCreate(d, desc, out);
}

bool makeImage(TrackedImage& img, Device& d, VkExtent2D extent, VkFormat format, uint32_t mips) {
    gpuImageDestroy(img.image, d);
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img.stage = VK_PIPELINE_STAGE_2_NONE;
    img.access = VK_ACCESS_2_NONE;
    return gpuImageCreate(img.image, d, extent, format,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mips);
}

bool viewBoth(DescriptorHeaps& heaps, Device& d, Slot slot, const GpuImage& img, VkDescriptorType type, uint32_t mips, uint32_t baseMip) {
    return heapWriteImage(heaps, d, slotOf(slot), type, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, mips, baseMip);
}

bool viewFlight(DescriptorHeaps& heaps, Device& d, uint32_t flight, Slot slot, const GpuImage& img, VkDescriptorType type) {
    return heapWriteImageFlight(heaps, d, flight, slotOf(slot), type, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1u, 0u);
}

void destroyImages(FsrPass& p, Device& d) {
    TrackedImage* imgs[] = {
        &p.motionVec, &p.dilatedMv, &p.dilatedDepth, &p.reconDepth, &p.farthest, &p.farthestMip,
        &p.luma[0], &p.luma[1], &p.spd, &p.atomic, &p.frameInfo, &p.shading, &p.dilatedReactive,
        &p.locks, &p.accumulation[0], &p.accumulation[1], &p.lumaHistory[0], &p.lumaHistory[1],
        &p.instabilityImg, &p.upscaled[0], &p.upscaled[1], &p.rcasIn, &p.output, &p.exposure,
        &p.reactive, &p.composition,
    };
    for (TrackedImage* img : imgs) {
        gpuImageDestroy(img->image, d);
        img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
        img->stage = VK_PIPELINE_STAGE_2_NONE;
        img->access = VK_ACCESS_2_NONE;
    }
}

bool createImages(FsrPass& p, Device& d, VkExtent2D render, VkExtent2D display) {
    destroyImages(p, d);
    const VkExtent2D one{1u, 1u};
    const VkExtent2D half{halfOf(render.width), halfOf(render.height)};
    const VkExtent2D pyramid = spdExtent(render.width, render.height);
    const bool ok = makeImage(p.motionVec, d, render, VK_FORMAT_R16G16_SFLOAT, 1)
        && makeImage(p.dilatedMv, d, render, VK_FORMAT_R16G16_SFLOAT, 1)
        && makeImage(p.dilatedDepth, d, render, VK_FORMAT_R32_SFLOAT, 1)
        && makeImage(p.reconDepth, d, render, VK_FORMAT_R32_UINT, 1)
        && makeImage(p.farthest, d, render, VK_FORMAT_R32_SFLOAT, 1)
        && makeImage(p.farthestMip, d, half, VK_FORMAT_R32_SFLOAT, 1)
        && makeImage(p.luma[0], d, render, VK_FORMAT_R16_SFLOAT, 1)
        && makeImage(p.luma[1], d, render, VK_FORMAT_R16_SFLOAT, 1)
        && makeImage(p.spd, d, pyramid, VK_FORMAT_R16G16_SFLOAT, 6)
        && makeImage(p.atomic, d, one, VK_FORMAT_R32_UINT, 1)
        && makeImage(p.frameInfo, d, one, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.shading, d, half, VK_FORMAT_R16_SFLOAT, 1)
        && makeImage(p.dilatedReactive, d, render, VK_FORMAT_R8G8B8A8_UNORM, 1)
        && makeImage(p.locks, d, render, VK_FORMAT_R8_UNORM, 1)
        && makeImage(p.accumulation[0], d, render, VK_FORMAT_R16_SFLOAT, 1)
        && makeImage(p.accumulation[1], d, render, VK_FORMAT_R16_SFLOAT, 1)
        && makeImage(p.lumaHistory[0], d, render, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.lumaHistory[1], d, render, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.instabilityImg, d, render, VK_FORMAT_R16_SFLOAT, 1)
        && makeImage(p.upscaled[0], d, display, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.upscaled[1], d, display, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.rcasIn, d, display, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.output, d, display, VK_FORMAT_R16G16B16A16_SFLOAT, 1)
        && makeImage(p.exposure, d, one, VK_FORMAT_R16G16_SFLOAT, 1)
        && makeImage(p.reactive, d, one, VK_FORMAT_R8_UNORM, 1)
        && makeImage(p.composition, d, one, VK_FORMAT_R8_UNORM, 1);
    if (!ok) {
        destroyImages(p, d);
        return false;
    }
    p.renderW = render.width;
    p.renderH = render.height;
    p.displayW = display.width;
    p.displayH = display.height;
    p.reset = true;
    p.parity = 0;
    p.frameIndex = 0.0f;
    p.jitterPhase = 0.0f;
    return true;
}

bool bindStatic(FsrPass& p, DescriptorHeaps& heaps, Device& d) {
    const bool spd =
        viewBoth(heaps, d, SpdS, p.spd.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, p.spd.image.mips, 0)
        && viewBoth(heaps, d, Spd0, p.spd.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, Spd1, p.spd.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 1)
        && viewBoth(heaps, d, Spd2, p.spd.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 2)
        && viewBoth(heaps, d, Spd3, p.spd.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 3)
        && viewBoth(heaps, d, Spd4, p.spd.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 4)
        && viewBoth(heaps, d, Spd5, p.spd.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 5);
    return spd
        && viewBoth(heaps, d, Motion, p.motionVec.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, DilMvS, p.dilatedMv.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, DilMvU, p.dilatedMv.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, DilDS, p.dilatedDepth.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, DilDU, p.dilatedDepth.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, ReconS, p.reconDepth.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, ReconU, p.reconDepth.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, FarS, p.farthest.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, FarU, p.farthest.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, FarMipS, p.farthestMip.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, FarMipU, p.farthestMip.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, AtomicU, p.atomic.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, FrameS, p.frameInfo.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, FrameU, p.frameInfo.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, ShadeS, p.shading.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, ShadeU, p.shading.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, ReactS, p.dilatedReactive.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, ReactU, p.dilatedReactive.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, LockS, p.locks.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, LockU, p.locks.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, InstS, p.instabilityImg.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, InstU, p.instabilityImg.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, RcasS, p.rcasIn.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, RcasU, p.rcasIn.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, OutS, p.output.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, OutU, p.output.image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0)
        && viewBoth(heaps, d, ExpS, p.exposure.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, MaskS, p.reactive.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0)
        && viewBoth(heaps, d, CompS, p.composition.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 0);
}

bool bindBuffers(const FsrPass& p, DescriptorHeaps& heaps, Device& d) {
    bool ok = true;
    for (uint32_t flight = 0; flight < 2u; ++flight) {
        const VkDeviceSize off = kStride * flight;
        ok = ok
            && heapWriteBufferFlight(heaps, d, flight, kFsrConstSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, p.constants.address + off, kStride)
            && heapWriteBufferFlight(heaps, d, flight, kFsrSpdSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, p.spdConst.address + off, kStride)
            && heapWriteBufferFlight(heaps, d, flight, kFsrRcasSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, p.rcasConst.address + off, kStride)
            && heapWriteBufferFlight(heaps, d, flight, kFsrMotionSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, p.motionConst.address + off, kStride);
    }
    return ok;
}

bool bindHistory(const FsrPass& p, DescriptorHeaps& heaps, Device& d, uint32_t flight) {
    const uint32_t read = p.parity & 1u;
    const uint32_t write = read ^ 1u;
    return viewFlight(heaps, d, flight, LumaS, p.luma[write].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        && viewFlight(heaps, d, flight, LumaU, p.luma[write].image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        && viewFlight(heaps, d, flight, LumaPrevS, p.luma[read].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        && viewFlight(heaps, d, flight, AccS, p.accumulation[read].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        && viewFlight(heaps, d, flight, AccU, p.accumulation[write].image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        && viewFlight(heaps, d, flight, HistS, p.lumaHistory[read].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        && viewFlight(heaps, d, flight, HistU, p.lumaHistory[write].image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        && viewFlight(heaps, d, flight, UpS, p.upscaled[read].image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        && viewFlight(heaps, d, flight, UpU, p.upscaled[write].image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}

bool createShaders(FsrPass& p, Device& d, const DescriptorHeaps& heaps) {
    PassBindings motion(heaps);
    uboAt(motion, 0, kFsrMotionSlot);
    motion.sampledImg(1, HeapImg::Depth);
    motion.storageImg(2, slotOf(Motion));

    PassBindings prepare(heaps);
    uboAt(prepare, 0, kFsrConstSlot);
    prepare.sampledImg(8, slotOf(Motion));
    prepare.sampledImg(9, HeapImg::Depth);
    prepare.sampledImg(10, HeapImg::HdrASampled);
    prepare.storageImg(24, slotOf(DilMvU));
    prepare.storageImg(25, slotOf(DilDU));
    prepare.storageImg(26, slotOf(ReconU));
    prepare.storageImg(27, slotOf(FarU));
    prepare.storageImg(28, slotOf(LumaU));

    PassBindings luma(heaps);
    uboAt(luma, 0, kFsrConstSlot);
    uboAt(luma, 1, kFsrSpdSlot);
    luma.sampledImg(8, slotOf(LumaS));
    luma.sampledImg(9, slotOf(FarS));
    luma.storageImg(24, slotOf(AtomicU));
    luma.storageImg(25, slotOf(FrameU));
    luma.storageImg(26, slotOf(Spd0));
    luma.storageImg(27, slotOf(Spd1));
    luma.storageImg(28, slotOf(Spd2));
    luma.storageImg(29, slotOf(Spd3));
    luma.storageImg(30, slotOf(Spd4));
    luma.storageImg(31, slotOf(Spd5));
    luma.storageImg(32, slotOf(FarMipU));

    PassBindings shadePyr(heaps);
    uboAt(shadePyr, 0, kFsrConstSlot);
    uboAt(shadePyr, 1, kFsrSpdSlot);
    shadePyr.sampledImg(8, slotOf(LumaS));
    shadePyr.sampledImg(9, slotOf(LumaPrevS));
    shadePyr.sampledImg(10, slotOf(DilMvS));
    shadePyr.sampledImg(11, slotOf(ExpS));
    shadePyr.storageImg(24, slotOf(AtomicU));
    shadePyr.storageImg(25, slotOf(Spd0));
    shadePyr.storageImg(26, slotOf(Spd1));
    shadePyr.storageImg(27, slotOf(Spd2));
    shadePyr.storageImg(28, slotOf(Spd3));
    shadePyr.storageImg(29, slotOf(Spd4));
    shadePyr.storageImg(30, slotOf(Spd5));

    PassBindings shade(heaps);
    uboAt(shade, 0, kFsrConstSlot);
    shade.sampledImg(8, slotOf(SpdS));
    shade.sampler(5, HeapSamp::Linear);
    shade.storageImg(24, slotOf(ShadeU));

    PassBindings react(heaps);
    uboAt(react, 0, kFsrConstSlot);
    react.sampledImg(8, slotOf(ReconS));
    react.sampledImg(9, slotOf(DilMvS));
    react.sampledImg(10, slotOf(DilDS));
    react.sampledImg(11, slotOf(MaskS));
    react.sampledImg(12, slotOf(CompS));
    react.sampledImg(13, slotOf(AccS));
    react.sampledImg(14, slotOf(ShadeS));
    react.sampledImg(15, slotOf(LumaS));
    react.sampledImg(16, slotOf(ExpS));
    react.sampler(5, HeapSamp::Linear);
    react.storageImg(24, slotOf(ReactU));
    react.storageImg(25, slotOf(LockU));
    react.storageImg(26, slotOf(AccU));

    PassBindings unstable(heaps);
    uboAt(unstable, 0, kFsrConstSlot);
    unstable.sampledImg(8, slotOf(ExpS));
    unstable.sampledImg(9, slotOf(ReactS));
    unstable.sampledImg(10, slotOf(DilMvS));
    unstable.sampledImg(12, slotOf(HistS));
    unstable.sampledImg(14, slotOf(LumaS));
    unstable.sampler(5, HeapSamp::Linear);
    unstable.storageImg(24, slotOf(HistU));
    unstable.storageImg(25, slotOf(InstU));

    PassBindings accum(heaps);
    uboAt(accum, 0, kFsrConstSlot);
    accum.sampledImg(8, slotOf(ExpS));
    accum.sampledImg(9, slotOf(ReactS));
    accum.sampledImg(10, slotOf(DilMvS));
    accum.sampledImg(11, slotOf(UpS));
    accum.sampledImg(13, slotOf(FarMipS));
    accum.sampledImg(14, slotOf(LumaS));
    accum.sampledImg(15, slotOf(InstS));
    accum.sampledImg(16, HeapImg::HdrASampled);
    accum.sampler(5, HeapSamp::Linear);
    accum.storageImg(24, slotOf(UpU));
    accum.storageImg(25, slotOf(RcasU));
    accum.storageImg(26, slotOf(LockU));

    PassBindings rcas(heaps);
    uboAt(rcas, 0, kFsrConstSlot);
    uboAt(rcas, 1, kFsrRcasSlot);
    rcas.sampledImg(8, slotOf(ExpS));
    rcas.sampledImg(9, slotOf(RcasS));
    rcas.storageImg(24, slotOf(OutU));

    return makeShader(d, motion, BH_SHADER_DIR "/fsr.motion.comp.spv", p.motion)
        && makeShader(d, prepare, BH_SHADER_DIR "/fsr.prepare.comp.spv", p.prepare)
        && makeShader(d, luma, BH_SHADER_DIR "/fsr.luma.comp.spv", p.lumaPyramid)
        && makeShader(d, shadePyr, BH_SHADER_DIR "/fsr.shading_pyramid.comp.spv", p.shadingPyramid)
        && makeShader(d, shade, BH_SHADER_DIR "/fsr.shading.comp.spv", p.shadingChange)
        && makeShader(d, react, BH_SHADER_DIR "/fsr.reactivity.comp.spv", p.reactivity)
        && makeShader(d, unstable, BH_SHADER_DIR "/fsr.instability.comp.spv", p.instability)
        && makeShader(d, accum, BH_SHADER_DIR "/fsr.accumulate.comp.spv", p.accumulate)
        && makeShader(d, rcas, BH_SHADER_DIR "/fsr.rcas.comp.spv", p.rcas);
}

void destroyShaders(FsrPass& p, Device& d) {
    shaderDestroy(d, p.motion);
    shaderDestroy(d, p.prepare);
    shaderDestroy(d, p.lumaPyramid);
    shaderDestroy(d, p.shadingPyramid);
    shaderDestroy(d, p.shadingChange);
    shaderDestroy(d, p.reactivity);
    shaderDestroy(d, p.instability);
    shaderDestroy(d, p.accumulate);
    shaderDestroy(d, p.rcas);
}

void upload(GpuBuffer& buf, Device& d, uint32_t flight, const void* data, uint32_t bytes) {
    if (buf.mapped == nullptr || flight > 1u) {
        return;
    }
    std::memcpy(static_cast<std::byte*>(buf.mapped) + kStride * flight, data, bytes);
    gpuBufferFlush(buf, d, kStride * flight, kStride);
}

void dispatch(VkCommandBuffer cmd, const ShaderExt& shader, uint32_t x, uint32_t y) {
    cmdBindCompute(cmd, shader.handle);
    vkCmdDispatch(cmd, x, y, 1u);
}

void memoryBarrier(VkCommandBuffer cmd) {
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    };
    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void clearFloat(VkCommandBuffer cmd, TrackedImage& img, float r, float g, float b, float a) {
    imageBarrier(cmd, img, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue color{};
    color.float32[0] = r;
    color.float32[1] = g;
    color.float32[2] = b;
    color.float32[3] = a;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, img.image.mips, 0u, 1u};
    vkCmdClearColorImage(cmd, img.image.image, VK_IMAGE_LAYOUT_GENERAL, &color, 1u, &range);
}

void clearUint(VkCommandBuffer cmd, TrackedImage& img) {
    imageBarrier(cmd, img, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue color{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vkCmdClearColorImage(cmd, img.image.image, VK_IMAGE_LAYOUT_GENERAL, &color, 1u, &range);
}

void useGeneral(VkCommandBuffer cmd, TrackedImage& img) {
    if (img.image.mips > 1u) {
        const VkImageLayout old = img.layout;
        rhiImageBarrierMips(cmd, img.image.image, old, VK_IMAGE_LAYOUT_GENERAL,
            old == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : img.stage,
            old == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_2_NONE : img.access,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            0u, img.image.mips);
        img.layout = VK_IMAGE_LAYOUT_GENERAL;
        img.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        img.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        return;
    }
    imageBarrier(cmd, img, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

const char* qualityName(uint32_t quality) {
    switch (quality) {
    case 1: return "Quality";
    case 2: return "Balanced";
    case 3: return "Performance";
    case 4: return "Ultra";
    default: return "Native";
    }
}

} // namespace

VkExtent2D fsrRenderExtent(uint32_t quality, VkExtent2D display) {
    const float scale[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    const uint32_t index = quality < 5u ? quality : 0u;
    const float div = scale[index];
    const uint32_t width = std::max(static_cast<uint32_t>(static_cast<float>(display.width) / div), 1u);
    const uint32_t height = std::max(static_cast<uint32_t>(static_cast<float>(display.height) / div), 1u);
    return {width, height};
}

void fsrDestroy(FsrPass& p, Device& d) {
    destroyShaders(p, d);
    destroyImages(p, d);
    gpuBufferDestroy(p.constants, d);
    gpuBufferDestroy(p.spdConst, d);
    gpuBufferDestroy(p.rcasConst, d);
    gpuBufferDestroy(p.motionConst, d);
    p.renderW = 0;
    p.renderH = 0;
    p.displayW = 0;
    p.displayH = 0;
    p.rebound = false;
    p.lastLog[0] = '\0';
}

bool fsrCreate(FsrPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent) {
    fsrDestroy(p, d);
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const bool buffers = gpuBufferCreate(p.constants, d, kStride * 2u, usage, true)
        && gpuBufferCreate(p.spdConst, d, kStride * 2u, usage, true)
        && gpuBufferCreate(p.rcasConst, d, kStride * 2u, usage, true)
        && gpuBufferCreate(p.motionConst, d, kStride * 2u, usage, true);
    if (!buffers || !createImages(p, d, extent, extent) || !bindBuffers(p, heaps, d) || !bindStatic(p, heaps, d) || !createShaders(p, d, heaps)) {
        spdlog::error("FSR 3.1.5 upscaler failed to create");
        fsrDestroy(p, d);
        return false;
    }
    return true;
}

bool fsrResize(FsrPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D render, VkExtent2D display) {
    if (p.prepare.handle == VK_NULL_HANDLE) {
        return false;
    }
    if (!createImages(p, d, render, display) || !bindStatic(p, heaps, d)) {
        spdlog::error("FSR resize failed");
        return false;
    }
    return true;
}

void fsrJitter(FsrPass& p, float& ndcX, float& ndcY, VkExtent2D render, VkExtent2D display) {
    ndcX = 0.0f;
    ndcY = 0.0f;
    if (p.prepare.handle == VK_NULL_HANDLE || render.width == 0 || render.height == 0 || display.width == 0) {
        return;
    }
    const float ratio = static_cast<float>(display.width) / static_cast<float>(render.width);
    int32_t phase = static_cast<int32_t>(8.0f * ratio * ratio);
    if (phase < 1) {
        phase = 1;
    }
    const int32_t index = static_cast<int32_t>(p.frameIndex);
    const int32_t sample = (index % phase) + 1;
    p.prevJitterX = p.jitterX;
    p.prevJitterY = p.jitterY;
    p.jitterX = halton(sample, 2) - 0.5f;
    p.jitterY = halton(sample, 3) - 0.5f;
    ndcX = p.jitterX * 2.0f / static_cast<float>(render.width);
    ndcY = p.jitterY * 2.0f / static_cast<float>(render.height);
}

void fsrRestoreColor(FsrPass& p, Device& d, DescriptorHeaps& heaps, uint32_t flight, const GpuImage& color) {
    if (!p.rebound || color.image == VK_NULL_HANDLE || flight > 1u) {
        return;
    }
    const bool ok =
        heapWriteImageFlight(heaps, d, flight, static_cast<uint32_t>(HeapImg::HdrASampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, color, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImageFlight(heaps, d, flight, static_cast<uint32_t>(HeapImg::HdrBSampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, color, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    if (ok) {
        p.rebound = false;
    }
}

void fsrRecord(
    VkCommandBuffer cmd,
    FsrPass& p,
    Device& d,
    DescriptorHeaps& heaps,
    uint32_t flight,
    TrackedImage& depth,
    TrackedImage& color,
    float dtSeconds,
    float fovYRad,
    float nearZ,
    const float unjitteredViewProj[16],
    const float jitteredInvViewProj[16]) {
    if (p.prepare.handle == VK_NULL_HANDLE || flight > 1u || p.renderW == 0 || p.displayW == 0) {
        return;
    }
    if (!bindHistory(p, heaps, d, flight)) {
        return;
    }
    const bool reset = p.reset;
    if (reset) {
        p.prevJitterX = p.jitterX;
        p.prevJitterY = p.jitterY;
        p.frameIndex = 0.0f;
        std::memcpy(p.prevUnjittered, unjitteredViewProj, sizeof(p.prevUnjittered));
        p.prevRender[0] = static_cast<int32_t>(p.renderW);
        p.prevRender[1] = static_cast<int32_t>(p.renderH);
        p.prevDisplay[0] = static_cast<int32_t>(p.displayW);
        p.prevDisplay[1] = static_cast<int32_t>(p.displayH);
    }
    const float ratio = static_cast<float>(p.displayW) / static_cast<float>(p.renderW);
    int32_t phase = static_cast<int32_t>(8.0f * ratio * ratio);
    if (phase < 1) {
        phase = 1;
    }
    if (reset || p.jitterPhase == 0.0f) {
        p.jitterPhase = static_cast<float>(phase);
    } else if (static_cast<float>(phase) > p.jitterPhase) {
        p.jitterPhase += 1.0f;
    } else if (static_cast<float>(phase) < p.jitterPhase) {
        p.jitterPhase -= 1.0f;
    }

    FsrConst constants{};
    constants.renderSize[0] = static_cast<int32_t>(p.renderW);
    constants.renderSize[1] = static_cast<int32_t>(p.renderH);
    constants.previousFrameRenderSize[0] = p.prevRender[0];
    constants.previousFrameRenderSize[1] = p.prevRender[1];
    constants.upscaleSize[0] = static_cast<int32_t>(p.displayW);
    constants.upscaleSize[1] = static_cast<int32_t>(p.displayH);
    constants.previousFrameUpscaleSize[0] = p.prevDisplay[0];
    constants.previousFrameUpscaleSize[1] = p.prevDisplay[1];
    constants.maxRenderSize[0] = constants.renderSize[0];
    constants.maxRenderSize[1] = constants.renderSize[1];
    constants.maxUpscaleSize[0] = constants.upscaleSize[0];
    constants.maxUpscaleSize[1] = constants.upscaleSize[1];
    const float epsilon = 1.19209290e-7f;
    const float cot = 1.0f / std::tan(fovYRad * 0.5f);
    const float aspect = static_cast<float>(p.renderW) / static_cast<float>(p.renderH);
    constants.deviceToViewDepth[0] = -epsilon;
    constants.deviceToViewDepth[1] = nearZ;
    constants.deviceToViewDepth[2] = aspect / cot;
    constants.deviceToViewDepth[3] = 1.0f / cot;
    constants.jitter[0] = p.jitterX;
    constants.jitter[1] = p.jitterY;
    constants.previousFrameJitter[0] = p.prevJitterX;
    constants.previousFrameJitter[1] = p.prevJitterY;
    constants.motionVectorScale[0] = 1.0f;
    constants.motionVectorScale[1] = 1.0f;
    constants.downscaleFactor[0] = static_cast<float>(p.renderW) / static_cast<float>(p.displayW);
    constants.downscaleFactor[1] = static_cast<float>(p.renderH) / static_cast<float>(p.displayH);
    constants.tanHalfFOV = std::tan(fovYRad * 0.5f);
    constants.jitterSequenceLength = p.jitterPhase;
    constants.deltaTime = std::clamp(dtSeconds, 0.0f, 1.0f);
    constants.deltaPreExposure = 1.0f;
    constants.viewSpaceToMetersFactor = 1.0f;
    constants.frameIndex = p.frameIndex;
    constants.velocityFactor = 1.0f;
    constants.reactivenessScale = 1.0f;
    constants.shadingChangeScale = 1.0f;
    constants.accumulationAddedPerFrame = 1.0f / 3.0f;
    constants.minDisocclusionAccumulation = -1.0f / 3.0f;

    const uint32_t mipLimit = 6u;
    uint32_t resolution = p.renderW > p.renderH ? p.renderW : p.renderH;
    uint32_t mips = 0u;
    while ((1u << mips) < resolution && mips < 12u) {
        mips += 1u;
    }
    if (mips > mipLimit) {
        mips = mipLimit;
    }
    const uint32_t spdX = groupsOf(p.renderW, 64u);
    const uint32_t spdY = groupsOf(p.renderH, 64u);
    SpdConst spd{};
    spd.mips = mips;
    spd.numWorkGroups = spdX * spdY;
    spd.renderSize[0] = p.renderW;
    spd.renderSize[1] = p.renderH;

    const float remapped = (-2.0f * std::clamp(p.sharpness, 0.0f, 1.0f)) + 2.0f;
    const float sharp = std::exp2(-remapped);
    const uint16_t halfSharp = floatToHalf(sharp);
    RcasConst rcas{};
    std::memcpy(&rcas.config[0], &sharp, sizeof(sharp));
    rcas.config[1] = static_cast<uint32_t>(halfSharp) | (static_cast<uint32_t>(halfSharp) << 16u);

    MotionConst motion{};
    std::memcpy(motion.currInv, jitteredInvViewProj, sizeof(motion.currInv));
    std::memcpy(motion.currUnjit, unjitteredViewProj, sizeof(motion.currUnjit));
    std::memcpy(motion.prevUnjit, p.prevUnjittered, sizeof(motion.prevUnjit));
    motion.width = p.renderW;
    motion.height = p.renderH;

    upload(p.constants, d, flight, &constants, sizeof(constants));
    upload(p.spdConst, d, flight, &spd, sizeof(spd));
    upload(p.rcasConst, d, flight, &rcas, sizeof(rcas));
    upload(p.motionConst, d, flight, &motion, sizeof(motion));

    imageBarrier(cmd, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    imageBarrier(cmd, color, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    TrackedImage* imgs[] = {
        &p.motionVec, &p.dilatedMv, &p.dilatedDepth, &p.reconDepth, &p.farthest, &p.farthestMip,
        &p.luma[0], &p.luma[1], &p.spd, &p.frameInfo, &p.shading, &p.dilatedReactive, &p.locks,
        &p.accumulation[0], &p.accumulation[1], &p.lumaHistory[0], &p.lumaHistory[1],
        &p.instabilityImg, &p.upscaled[0], &p.upscaled[1], &p.rcasIn, &p.output,
    };
    for (TrackedImage* img : imgs) {
        useGeneral(cmd, *img);
    }
    if (reset || p.exposure.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        clearFloat(cmd, p.exposure, 1.0f, 1.0f, 1.0f, 1.0f);
        clearFloat(cmd, p.reactive, 0.0f, 0.0f, 0.0f, 0.0f);
        clearFloat(cmd, p.composition, 0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        useGeneral(cmd, p.exposure);
        useGeneral(cmd, p.reactive);
        useGeneral(cmd, p.composition);
    }

    const uint32_t srcX = groupsOf(p.renderW, 8u);
    const uint32_t srcY = groupsOf(p.renderH, 8u);
    const uint32_t dstX = groupsOf(p.displayW, 8u);
    const uint32_t dstY = groupsOf(p.displayH, 8u);
    const uint32_t shadeX = groupsOf(halfOf(p.renderW), 8u);
    const uint32_t shadeY = groupsOf(halfOf(p.renderH), 8u);

    dispatch(cmd, p.motion, srcX, srcY);
    memoryBarrier(cmd);
    dispatch(cmd, p.prepare, srcX, srcY);
    memoryBarrier(cmd);
    clearUint(cmd, p.atomic);
    memoryBarrier(cmd);
    dispatch(cmd, p.lumaPyramid, spdX, spdY);
    memoryBarrier(cmd);
    clearUint(cmd, p.atomic);
    memoryBarrier(cmd);
    dispatch(cmd, p.shadingPyramid, spdX, spdY);
    memoryBarrier(cmd);
    dispatch(cmd, p.shadingChange, shadeX, shadeY);
    memoryBarrier(cmd);
    dispatch(cmd, p.reactivity, srcX, srcY);
    memoryBarrier(cmd);
    dispatch(cmd, p.instability, srcX, srcY);
    memoryBarrier(cmd);
    dispatch(cmd, p.accumulate, dstX, dstY);
    memoryBarrier(cmd);
    dispatch(cmd, p.rcas, groupsOf(p.displayW, 16u), groupsOf(p.displayH, 16u));

    imageBarrier(cmd, p.output, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    const bool rebound =
        heapWriteImageFlight(heaps, d, flight, static_cast<uint32_t>(HeapImg::HdrASampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, p.output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImageFlight(heaps, d, flight, static_cast<uint32_t>(HeapImg::HdrBSampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, p.output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    p.rebound = rebound;

    p.prevRender[0] = constants.renderSize[0];
    p.prevRender[1] = constants.renderSize[1];
    p.prevDisplay[0] = constants.upscaleSize[0];
    p.prevDisplay[1] = constants.upscaleSize[1];
    std::memcpy(p.prevUnjittered, unjitteredViewProj, sizeof(p.prevUnjittered));
    p.frameIndex += 1.0f;
    p.parity ^= 1u;
    p.reset = false;
    std::snprintf(p.lastLog, sizeof(p.lastLog), "FSR 3.1.5 %s %ux%u -> %ux%u резк %.2f",
        qualityName(p.quality), p.renderW, p.renderH, p.displayW, p.displayH, p.sharpness);
}

} // namespace burnhope
