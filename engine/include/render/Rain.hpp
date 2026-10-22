#pragma once

#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

#include <cstdint>

namespace burnhope {

constexpr uint32_t kRainCap = 4096;
constexpr uint32_t kRainStride = 64;
constexpr uint32_t kRainSlice = kRainCap * kRainStride;
constexpr uint32_t kRainSlot = 83;
constexpr uint32_t kRainGroups = kRainCap / 32u;

struct alignas(16) RainPush {
    uint32_t flight = 0;
    uint32_t mode = 0;
    float dt = 0.0f;
    uint32_t pad = 0;
};
static_assert(sizeof(RainPush) == 16);

struct RainPass {
    ShaderExt sim{};
    ShaderExt mesh{};
    ShaderExt frag{};
    GpuBuffer drops{};
    uint8_t gpuLive = 0;
};

[[nodiscard]] bool rainCreate(RainPass& pass, Device& device, DescriptorHeaps& heaps);
void rainDestroy(RainPass& pass, Device& device);
void rainRecord(
    VkCommandBuffer cmd,
    RainPass& pass,
    TrackedImage& hdr,
    VkExtent2D extent,
    uint32_t flight,
    float dt);

} // namespace burnhope
