#pragma once

#include "gpu_scene/InstanceData.hpp"
#include "gpu_scene/MeshletGpu.hpp"
#include "render/PassCommon.hpp"
#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

// World-space hash of radiance intervals. One BLAS of the static scene, ray query on RT cores.
struct RadiancePass {
    GpuBuffer table{};
    GpuBuffer indices{};
    GpuBuffer scratch{};
    GpuBuffer asBuf{};
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    ShaderExt trace{};
    ShaderExt apply{};
    uint32_t triangles = 0;
    uint32_t slots = 262144;
    bool geometry = false;
    bool hashLive = false;
    // Кадр уже добавил хеш в картинку. Следующий луч ставится только после этого.
    bool hashDrawn = false;
    // Fence луча сигналит. Пока кадр не прочитал хеш, чистка не стартует.
    bool traceReady = false;
    GpuTask buildTask{};
    GpuTask traceTask{};
};

[[nodiscard]] bool radianceCreate(RadiancePass& p, Device& d, const DescriptorHeaps& heaps);
void radianceDestroy(RadiancePass& p, Device& d);
// Печёт треугольники и записывает BLAS в свой буфер. В командный буфер кадра не кладёт.
void radianceArm(
    RadiancePass& p,
    Device& d,
    DescriptorHeaps& heaps,
    const GpuVertex* verts,
    const uint32_t* indices,
    const GpuInstance* instances,
    const GpuMeshlet* meshlets,
    uint32_t instanceCount);
// Сабмит сборки и луча после present кадра. Пока fence сборки не сигналит, geometry остаётся false.
void radianceKick(RadiancePass& p, Device& d);
// Пишет чистку хэша и луч в свой буфер. В командный буфер кадра не кладёт.
void radianceTraceArm(RadiancePass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent, uint32_t flight);
// В кадре только добавка в HdrA, и только если прошлый луч уже дописан.
void radianceRecord(VkCommandBuffer cmd, VkExtent2D extent, RadiancePass& p, bool freeze);

} // namespace burnhope
