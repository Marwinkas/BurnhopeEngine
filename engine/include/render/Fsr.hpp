#pragma once

#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

#include <cstdint>

namespace burnhope {

struct Device;
struct DescriptorHeaps;

constexpr uint32_t kPassFsr = 1u << 22u;
constexpr uint32_t kFsrImgBase = 928u;
constexpr uint32_t kFsrConstSlot = 64u;
constexpr uint32_t kFsrSpdSlot = 65u;
constexpr uint32_t kFsrRcasSlot = 66u;
constexpr uint32_t kFsrMotionSlot = 67u;

struct FsrPass {
    ShaderExt motion{};
    ShaderExt prepare{};
    ShaderExt lumaPyramid{};
    ShaderExt shadingPyramid{};
    ShaderExt shadingChange{};
    ShaderExt reactivity{};
    ShaderExt instability{};
    ShaderExt accumulate{};
    ShaderExt rcas{};
    GpuBuffer constants{};
    GpuBuffer spdConst{};
    GpuBuffer rcasConst{};
    GpuBuffer motionConst{};
    TrackedImage motionVec{};
    TrackedImage dilatedMv{};
    TrackedImage dilatedDepth{};
    TrackedImage reconDepth{};
    TrackedImage farthest{};
    TrackedImage farthestMip{};
    TrackedImage luma[2]{};
    TrackedImage spd{};
    TrackedImage atomic{};
    TrackedImage frameInfo{};
    TrackedImage shading{};
    TrackedImage dilatedReactive{};
    TrackedImage locks{};
    TrackedImage accumulation[2]{};
    TrackedImage lumaHistory[2]{};
    TrackedImage instabilityImg{};
    TrackedImage upscaled[2]{};
    TrackedImage rcasIn{};
    TrackedImage output{};
    TrackedImage exposure{};
    TrackedImage reactive{};
    TrackedImage composition{};
    uint32_t quality = 0;
    float sharpness = 0.2f;
    uint32_t renderW = 0;
    uint32_t renderH = 0;
    uint32_t displayW = 0;
    uint32_t displayH = 0;
    uint32_t parity = 0;
    float frameIndex = 0.0f;
    float jitterPhase = 0.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    float prevJitterX = 0.0f;
    float prevJitterY = 0.0f;
    int32_t prevRender[2]{};
    int32_t prevDisplay[2]{};
    float prevUnjittered[16]{};
    bool reset = true;
    bool rebound = false;
    char lastLog[160]{};
};

[[nodiscard]] VkExtent2D fsrRenderExtent(uint32_t quality, VkExtent2D display);
[[nodiscard]] bool fsrCreate(FsrPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent);
void fsrDestroy(FsrPass& p, Device& d);
[[nodiscard]] bool fsrResize(FsrPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D render, VkExtent2D display);
void fsrJitter(FsrPass& p, float& ndcX, float& ndcY, VkExtent2D render, VkExtent2D display);
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
    const float jitteredInvViewProj[16]);
void fsrRestoreColor(FsrPass& p, Device& d, DescriptorHeaps& heaps, uint32_t flight, const GpuImage& color);

} // namespace burnhope
