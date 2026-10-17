#pragma once

#include "gpu_scene/FrameUniforms.hpp"
#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

constexpr uint32_t kGtaoConstSlot = 68u;

struct alignas(16) GtaoConstants {
    int32_t viewportW = 0;
    int32_t viewportH = 0;
    float pixelW = 0.0f;
    float pixelH = 0.0f;
    float depthMul = 0.0f;
    float depthAdd = 0.0f;
    float tanX = 0.0f;
    float tanY = 0.0f;
    float ndcMulX = 0.0f;
    float ndcMulY = 0.0f;
    float ndcAddX = 0.0f;
    float ndcAddY = 0.0f;
    float ndcPxX = 0.0f;
    float ndcPxY = 0.0f;
    float radius = 0.5f;
    float falloff = 0.615f;
    float radiusMul = 1.457f;
    float pad0 = 0.0f;
    float power = 2.2f;
    float denoiseBeta = 1.2f;
    float samplePower = 2.0f;
    float thin = 0.0f;
    float mipOffset = 3.30f;
    int32_t noise = 0;
};
static_assert(sizeof(GtaoConstants) == 96);
static_assert(alignof(GtaoConstants) == 16);

struct GtaoPass {
    ShaderExt prefilter{};
    ShaderExt normals{};
    ShaderExt main[4]{};
    ShaderExt denoise[2]{};
    ShaderExt denoiseLast[2]{};
    ShaderExt resolve[2]{};
    TrackedImage depthMips{};
    TrackedImage normalsImg{};
    TrackedImage aoTerm[2]{};
    TrackedImage edges{};
    GpuBuffer constants{};
    uint32_t frame = 0;
    uint32_t mips = 0;
    char lastLog[96]{};
};

[[nodiscard]] bool gtaoCreate(GtaoPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent);
void gtaoDestroy(GtaoPass& p, Device& d);
[[nodiscard]] bool gtaoResize(GtaoPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent);
void gtaoRecord(
    VkCommandBuffer cmd,
    GtaoPass& p,
    Device& d,
    uint32_t flight,
    TrackedImage& ao,
    bool on,
    float fovY,
    float zn,
    const GtaoParams& params);

} // namespace burnhope
