#pragma once

#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

#include <cstdint>

namespace burnhope {

struct DescriptorHeaps;

constexpr uint32_t kFogW = 128;
constexpr uint32_t kFogH = 72;
constexpr uint32_t kFogD = 48;
constexpr uint32_t kFogScatterSlot = 933;
constexpr uint32_t kFogIntSlot = 934;
constexpr uint32_t kFogIntSampleSlot = 935;

struct FogVolume {
    TrackedImage scatter{};
    TrackedImage integrated{};
    ShaderExt scatterCs{};
    ShaderExt integrateCs{};
    ShaderExt applyCs{};
    bool ready = false;
};

[[nodiscard]] bool fogCreate(FogVolume& fog, Device& device, DescriptorHeaps& heaps);
void fogDestroy(FogVolume& fog, Device& device);
void fogRecord(VkCommandBuffer cmd, FogVolume& fog, TrackedImage& hdr, VkExtent2D extent);

} // namespace burnhope
