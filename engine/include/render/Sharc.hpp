#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct DescriptorHeaps;

// Профиль GI «SHARC» в меню. Бит не пересекается с ReSTIR (20/21) и FSR (22).
constexpr uint32_t kPassSharc = 1u << 23;
// Слоты 64..69 заняты FSR, цветокором и XeGTAO. Кэш сидит сразу за ними.
constexpr uint32_t kSharcHashSlot = 70u;
constexpr uint32_t kSharcAccumSlot = 71u;
constexpr uint32_t kSharcResolvedSlot = 72u;
constexpr uint32_t kSharcParamSlot = 73u;
// 2^22 записей, как в сэмпле RTXGI. Хеш 8 байт, накопление и resolved по 16.
constexpr uint32_t kSharcCapacity = 1u << 22;

// 80 байт, скалярная раскладка. Поля совпадают с SharcGpuParams в sharc_params.slang.
struct alignas(16) SharcGpuParams {
    float camX = 0.0f;
    float camY = 0.0f;
    float camZ = 0.0f;
    float sceneScale = 50.0f;
    float prevX = 0.0f;
    float prevY = 0.0f;
    float prevZ = 0.0f;
    float levelBias = 0.0f;
    uint32_t capacity = kSharcCapacity;
    uint32_t accumulationFrameNum = 20u;
    uint32_t staleFrameNum = 60u;
    uint32_t frameIndex = 0u;
    float radianceScale = 1000.0f;
    float roughnessThreshold = 0.4f;
    uint32_t downscale = 5u;
    uint32_t bouncesMax = 4u;
    uint32_t tlasLo = 0u;
    uint32_t tlasHi = 0u;
    float giGain = 1.0f;
    uint32_t lightCount = 0u;
    float prevVp[16]{};
};
static_assert(sizeof(SharcGpuParams) == 144);
static_assert(alignof(SharcGpuParams) == 16);
static_assert(offsetof(SharcGpuParams, prevVp) == 80);

struct SharcPass {
    GpuBuffer hash{};
    GpuBuffer accum{};
    GpuBuffer resolved{};
    GpuBuffer params{};
    ShaderExt update{};
    ShaderExt resolve{};
    ShaderExt query{};
    ShaderExt relaxTemporal{};
    ShaderExt relaxSpatial{};
    TrackedImage raw{};
    TrackedImage history{};
    TrackedImage temporal{};
    float prevVp[16]{};
    float prevX = 0.0f;
    float prevY = 0.0f;
    float prevZ = 0.0f;
    uint32_t frameIndex = 0u;
    bool hasPrev = false;
    bool cleared = false;
    bool live = false;
    char lastLog[96]{};
};

[[nodiscard]] bool sharcCreate(SharcPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent);
[[nodiscard]] bool sharcResize(SharcPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent);
void sharcDestroy(SharcPass& p, Device& d);
void sharcRecord(
    VkCommandBuffer cmd,
    SharcPass& p,
    Device& d,
    DescriptorHeaps& heaps,
    uint32_t flight,
    VkExtent2D extent,
    float camX,
    float camY,
    float camZ,
    const float viewProj[16],
    float giGain,
    uint32_t lightCount,
    VkDeviceAddress tlas,
    bool freeze);

} // namespace burnhope
