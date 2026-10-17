#pragma once

#include "gpu_scene/InstanceData.hpp"
#include "gpu_scene/MeshletGpu.hpp"
#include "render/PassCommon.hpp"
#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

// 64 бита: 18 X, 18 Y, 18 Z, 9 LOD. Бит 63 занят, чтобы старшее слово ключа не было нулём.
struct RcProbeKey {
    uint64_t bits = 0;
};
static_assert(sizeof(RcProbeKey) == 8);

// Запись зонда в срезе HeapBuf::Rc. 64 байта, выравнивание 16.
// origin/normal — точка G-буфера, с которой стреляет полная сетка направлений.
struct alignas(16) RcProbeData {
    int32_t cellX = 0;
    int32_t cellY = 0;
    int32_t cellZ = 0;
    uint32_t lod = 0;
    uint32_t parent = 0xffffffffu;
    uint32_t rayCount = 0;
    uint32_t flags = 0;
    uint32_t pad = 0;
    float origin[4]{};
    float normal[4]{};
};
static_assert(sizeof(RcProbeData) == 64);
static_assert(alignof(RcProbeData) == 16);

// Накопитель одного направления. Фиксированная точка, атомарные сложения.
struct alignas(16) RcDirAccum {
    uint32_t radR = 0;
    uint32_t radG = 0;
    uint32_t radB = 0;
    uint32_t betaSum = 0;
    uint32_t count = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};
static_assert(sizeof(RcDirAccum) == 32);
static_assert(alignof(RcDirAccum) == 16);

struct alignas(16) RcMerged {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float valid = 0.0f;
};
static_assert(sizeof(RcMerged) == 16);

// 16 байт. Индекс каскада, шаг зонда, t0, номер среза.
// Бит 0 frameIndex — номер flight. Срез печётся один раз и дальше только читается.
struct RcPushConstants {
    uint32_t cascadeIndex = 0;
    float baseSpacing = 0.2f;
    float baseLength = 0.72f;
    uint32_t frameIndex = 0;
};
static_assert(sizeof(RcPushConstants) == 16);

// Split Radiance Cascades. Один BLAS статической сцены, луч — ray query из compute.
// Буфер устройства сидит в HeapBuf::Rc (слот 18). Два среза: каждый печётся один раз.
struct RadiancePass {
    GpuBuffer table{};
    GpuBuffer staging{};
    GpuBuffer indices{};
    GpuBuffer scratch{};
    GpuBuffer asBuf{};
    GpuBuffer check{};
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    GpuBuffer tlasBuf{};
    GpuBuffer tlasScratch{};
    GpuBuffer tlasInst{};
    ShaderExt alloc{};
    ShaderExt prefix{};
    ShaderExt trace{};
    ShaderExt merge{};
    ShaderExt octahedral{};
    ShaderExt shade{};
    RcPushConstants push{};
    uint32_t posByte = 0;
    uint32_t triangles = 0;
    uint32_t sliceStride = 0;
    uint32_t clearBytes = 0;
    uint32_t temporal = 0;
    uint32_t slots = 32768;
    float logSpace = -1.0f;
    float logReach = -1.0f;
    float logRays = -1.0f;
    float logGain = -1.0f;
    float logSun = -1.0f;
    float logBounce = -1.0f;
    bool geometry = false;
    uint8_t pause = 0;
    uint8_t bakedMask = 0;
    bool hashLive = false;
    float bakeSpacing = -1.0f;
    float bakeT0 = -1.0f;
    float bakeSun = -1.0f;
    float bakeBounce = -1.0f;
    float bakeSunX = -1.0f;
    float bakeSunY = -1.0f;
    float bakeSunZ = -1.0f;
    GpuTask buildTask{};
};

[[nodiscard]] bool radianceCreate(RadiancePass& p, Device& d, const DescriptorHeaps& heaps);
void radianceDestroy(RadiancePass& p, Device& d);
// Снимает BLAS, когда кадры, которые его читали, уже не в полёте.
void radianceDrop(RadiancePass& p, Device& d);
// Печёт треугольники и записывает BLAS. В командный буфер кадра не кладёт.
void radianceArm(
    RadiancePass& p,
    Device& d,
    DescriptorHeaps& heaps,
    const GpuVertex* verts,
    const uint32_t* indices,
    const GpuInstance* instances,
    const GpuMeshlet* meshlets,
    uint32_t instanceCount,
    uint32_t vertCount,
    uint32_t indexCount,
    uint32_t meshletCount);
// Сабмит сборки. Пока fence не сигналит, geometry остаётся false.
void radianceKick(RadiancePass& p, Device& d);
// Печь мира один раз на срез: треугольники, ray split, слияние, октаэдр. Потом только shade.
void radianceTraceArm(
    RadiancePass& p,
    Device& d,
    DescriptorHeaps& heaps,
    VkExtent2D extent,
    uint32_t flight,
    float rays,
    float spacing,
    float intensity,
    float maxDist,
    float sunScale,
    float bounce,
    float sunX,
    float sunY,
    float sunZ,
    VkCommandBuffer cmd);
// Восьмой тап по зондам в HdrA. Только если этот кадр уже дописал октаэдр.
void radianceRecord(VkCommandBuffer cmd, VkExtent2D extent, RadiancePass& p, bool freeze);

} // namespace burnhope
