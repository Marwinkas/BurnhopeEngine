#pragma once

#include "render/PassCommon.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

// 64 байта. Якорь и scroll — в вокселях этого клипмапа. Слэб по оси пуст, если min == max.
struct alignas(16) VxCascade {
    int32_t anchorX = 0;
    int32_t anchorY = 0;
    int32_t anchorZ = 0;
    int32_t scrollX = 0;
    int32_t scrollY = 0;
    int32_t scrollZ = 0;
    float voxel = 0.125f;
    float extent = 16.0f;
    int32_t slabMinX = 0;
    int32_t slabMinY = 0;
    int32_t slabMinZ = 0;
    uint32_t full = 1;
    int32_t slabMaxX = 0;
    int32_t slabMaxY = 0;
    int32_t slabMaxZ = 0;
    int32_t pad = 0;
};
static_assert(sizeof(VxCascade) == 64);
static_assert(alignof(VxCascade) == 16);

// 16 байт. Каскад и flight в одном шейдере. instances — число GpuInstance.
struct VxPush {
    uint32_t cascade = 0;
    uint32_t flight = 0;
    uint32_t instances = 0;
    uint32_t flags = 0;
};
static_assert(sizeof(VxPush) == 16);

// Три клипмапа 128³. Уровень 0 — 16 м, 1 — 64 м, 2 — 256 м. Два flight, чтобы соседний кадр не писал чужой объём.
struct VoxelPass {
    TrackedImage volume[2][3]{};
    GpuBuffer atoms{};
    GpuBuffer clip{};
    ShaderExt clear{};
    ShaderExt voxel{};
    ShaderExt resolve{};
    ShaderExt trace{};
    int32_t anchor[2][3][3]{};
    int32_t scroll[2][3][3]{};
    uint8_t live[2][3]{};
    float sun[4]{};
    uint8_t sunSet = 0;
    uint32_t bakedInstances = 0;
};

[[nodiscard]] bool voxelCreate(VoxelPass& p, Device& d, const DescriptorHeaps& heaps);
void voxelDestroy(VoxelPass& p, Device& d);
// Пишет клипмап, если камера вошла в новый воксель, и трассирует конусы в HdrA.
void voxelRecord(
    VkCommandBuffer cmd,
    VoxelPass& p,
    Device& d,
    VkExtent2D extent,
    uint32_t flight,
    uint32_t instances,
    bool moved,
    const FrameView& view,
    const FrameSun& sun);

} // namespace burnhope
