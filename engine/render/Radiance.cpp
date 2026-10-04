#include "render/Radiance.hpp"
#include "ui/Model.hpp"

#include "render/PassBufferSync.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapBind.hpp"

static_assert(static_cast<uint32_t>(burnhope::HeapBuf::Rc) != burnhope::kUiDescPrims, "RC buffer slot overwrites the UI primitive buffer");

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace burnhope {

namespace {

constexpr uint32_t kRcHeader = 256;
constexpr uint32_t kHashCap = 65536;
constexpr uint32_t kProbeCap[4] = {16384u, 4096u, 1024u, 256u};
constexpr uint32_t kDirCount[4] = {32u, 128u, 512u, 2048u};
constexpr uint32_t kHashStride = 16;
constexpr uint32_t kMetaStride = 32;
constexpr uint32_t kDirStride = 16;
constexpr uint32_t kIrrStride = 36u * 16u;
constexpr uint32_t kMaxTris = 3000000;

struct RcArena {
    uint32_t hashBase[4]{};
    uint32_t metaBase[4]{};
    uint32_t dirBase[4]{};
    uint32_t irrBase = 0;
    uint32_t end = 0;
};

RcArena rcArena() {
    RcArena arena{};
    uint32_t cursor = kRcHeader;
    for (uint32_t c = 0; c < 4u; ++c) {
        arena.hashBase[c] = cursor;
        cursor += kHashCap * kHashStride;
    }
    for (uint32_t c = 0; c < 4u; ++c) {
        arena.metaBase[c] = cursor;
        cursor += kProbeCap[c] * kMetaStride;
    }
    for (uint32_t c = 0; c < 4u; ++c) {
        arena.dirBase[c] = cursor;
        cursor += kProbeCap[c] * kDirCount[c] * kDirStride;
    }
    arena.irrBase = cursor;
    cursor += kProbeCap[0] * kIrrStride;
    arena.end = (cursor + 255u) & ~255u;
    return arena;
}

static_assert(kDirCount[0] * kDirStride == 512u);

struct PackedPos {
    float x, y, z;
    uint32_t material = 0;
};

void transform(const float* m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[1] * y + m[2] * z + m[3];
    oy = m[4] * x + m[5] * y + m[6] * z + m[7];
    oz = m[8] * x + m[9] * y + m[10] * z + m[11];
}

} // namespace

bool radianceCreate(RadiancePass& p, Device& d, const DescriptorHeaps& heaps) {
    radianceDestroy(p, d);
    if (!d.caps.rayQuery) {
        spdlog::info("radiance: ray query is off, screen cascades stay");
        return true;
    }
    PassBindings trace{heaps};
    trace.ubo(0, HeapBuf::Frame);
    trace.sun();
    trace.sampledImg(1, HeapImg::Vis);
    trace.sampledImg(2, HeapImg::Depth);
    trace.storageBuf(14, HeapBuf::Materials);
    trace.storageBuf(30, HeapBuf::Rc, false);
    trace.knob(54, HeapBuf::RcParams);
    PassBindings apply{heaps};
    apply.ubo(0, HeapBuf::Frame);
    apply.sampledImg(1, HeapImg::Vis);
    apply.sampledImg(2, HeapImg::Depth);
    apply.storageImg(3, HeapImg::HdrAStorage);
    apply.storageBuf(10, HeapBuf::Instances);
    apply.storageBuf(14, HeapBuf::Materials);
    apply.storageBuf(30, HeapBuf::Rc, false);
    apply.knob(54, HeapBuf::RcParams);
    const ShaderCreateDesc traceDesc{
        .path = BH_SHADER_DIR "/radiance.trace.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = trace.raw.count,
        .mappings = trace.raw.mappings,
    };
    const ShaderCreateDesc applyDesc{
        .path = BH_SHADER_DIR "/radiance.apply.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = apply.raw.count,
        .mappings = apply.raw.mappings,
    };
    if (!shaderCreate(d, traceDesc, p.trace) || !shaderCreate(d, applyDesc, p.apply)) {
        spdlog::error("radiance shaders failed");
        radianceDestroy(p, d);
        return true;
    }
    p.slots = kHashCap;
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = 8;
    VkPipelineLayoutCreateInfo layout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range,
    };
    if (vkCreatePipelineLayout(d.device, &layout, nullptr, &p.pushLayout) != VK_SUCCESS) {
        spdlog::error("radiance push layout failed");
        p.pushLayout = VK_NULL_HANDLE;
    }
    PassBindings mergeBind{heaps};
    mergeBind.storageBuf(30, HeapBuf::Rc, false);
    mergeBind.knob(54, HeapBuf::RcParams);
    const ShaderCreateDesc mergeDesc{
        .path = BH_SHADER_DIR "/radiance.merge.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = mergeBind.raw.count,
        .mappings = mergeBind.raw.mappings,
        .pushBytes = 8,
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    PassBindings irrBind{heaps};
    irrBind.storageBuf(30, HeapBuf::Rc, false);
    const ShaderCreateDesc irrDesc{
        .path = BH_SHADER_DIR "/radiance.irr.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = irrBind.raw.count,
        .mappings = irrBind.raw.mappings,
    };
    if (!shaderCreate(d, mergeDesc, p.merge) || !shaderCreate(d, irrDesc, p.irradiance)) {
        spdlog::error("radiance merge shader failed");
        radianceDestroy(p, d);
        return true;
    }
    return true;
}

void radianceDestroy(RadiancePass& p, Device& d) {
    gpuTaskDestroy(p.buildTask, d);
    gpuTaskDestroy(p.traceTask, d);
    if (p.blas != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(d.device, p.blas, nullptr);
    }
    if (p.pushLayout != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(d.device, p.pushLayout, nullptr);
    }
    p.pushLayout = VK_NULL_HANDLE;
    p.blas = VK_NULL_HANDLE;
    gpuBufferDestroy(p.table, d);
    gpuBufferDestroy(p.indices, d);
    gpuBufferDestroy(p.scratch, d);
    gpuBufferDestroy(p.asBuf, d);
    shaderDestroy(d, p.irradiance);
    shaderDestroy(d, p.merge);
    shaderDestroy(d, p.trace);
    shaderDestroy(d, p.apply);
    p.posByte = 0;
    p.triangles = 0;
    p.geometry = false;
    p.hashLive = false;
    p.hashDrawn = false;
    p.traceReady = false;
}

void radianceKick(RadiancePass& p, Device& d) {
    if (gpuTaskDone(p.buildTask, d)) {
        p.geometry = true;
    } else if (p.buildTask.stage == 1) {
        if (!gpuTaskSubmit(p.buildTask, d)) {
            spdlog::error("radiance build submit failed");
            return;
        }
        spdlog::info("radiance build queued, {} triangles", p.triangles);
        return;
    }
    if (!p.geometry || p.traceTask.stage != 1) {
        return;
    }
    if (!gpuTaskSubmit(p.traceTask, d)) {
        spdlog::error("radiance trace submit failed");
    }
}

void radianceArm(
    RadiancePass& p,
    Device& d,
    DescriptorHeaps& heaps,
    const GpuVertex* verts,
    const uint32_t* indices,
    const GpuInstance* instances,
    const GpuMeshlet* meshlets,
    uint32_t instanceCount) {
    if (p.geometry || p.buildTask.stage != 0 || !d.caps.rayQuery || p.trace.handle == VK_NULL_HANDLE) {
        return;
    }
    if (verts == nullptr || indices == nullptr || instances == nullptr || meshlets == nullptr || instanceCount == 0) {
        return;
    }
    std::vector<PackedPos> positions;
    std::vector<uint32_t> tris;
    positions.reserve(65536);
    tris.reserve(65536);
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const uint32_t mesh = instances[i].meshId;
        if (mesh >= instanceCount) {
            continue;
        }
        const GpuMeshlet& mlet = meshlets[mesh];
        const float* m = instances[i].world;
        for (uint32_t t = 0; t < mlet.triangleCount; ++t) {
            if (positions.size() / 3u >= kMaxTris) {
                break;
            }
            const uint32_t at = mlet.indexOffset + t * 3u;
            for (uint32_t k = 0; k < 3u; ++k) {
                const GpuVertex& v = verts[mlet.vertexOffset + indices[at + k]];
                PackedPos out{};
                transform(m, v.px, v.py, v.pz, out.x, out.y, out.z);
                out.material = instances[i].materialId;
                positions.push_back(out);
            }
            const uint32_t base = static_cast<uint32_t>(positions.size() - 3u);
            tris.push_back(base);
            tris.push_back(base + 1u);
            tris.push_back(base + 2u);
        }
    }
    p.triangles = static_cast<uint32_t>(tris.size() / 3u);
    if (p.triangles == 0) {
        spdlog::error("radiance: no triangles");
        return;
    }
    const RcArena arena = rcArena();
    const uint32_t posByte = arena.end;
    p.posByte = posByte;
    const VkDeviceSize tableBytes = posByte + positions.size() * sizeof(PackedPos);
    const VkBufferUsageFlags geoUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!gpuBufferCreate(p.table, d, tableBytes, geoUsage, true)
        || !gpuBufferCreate(p.indices, d, tris.size() * sizeof(uint32_t), geoUsage, true)) {
        spdlog::error("radiance buffers failed");
        return;
    }
    std::memcpy(static_cast<std::byte*>(p.table.mapped) + posByte, positions.data(), positions.size() * sizeof(PackedPos));
    std::memcpy(p.indices.mapped, tris.data(), tris.size() * sizeof(uint32_t));
    auto* head = static_cast<uint32_t*>(p.table.mapped);
    head[2] = kHashCap;
    head[3] = posByte;
    head[6] = arena.hashBase[0];
    head[7] = arena.hashBase[1];
    head[8] = arena.hashBase[2];
    head[9] = arena.hashBase[3];
    head[10] = arena.metaBase[0];
    head[11] = arena.metaBase[1];
    head[12] = arena.metaBase[2];
    head[13] = arena.metaBase[3];
    head[14] = arena.dirBase[0];
    head[15] = arena.dirBase[1];
    head[16] = arena.dirBase[2];
    head[17] = arena.dirBase[3];
    head[18] = arena.irrBase;
    head[19] = kProbeCap[0];
    head[20] = kProbeCap[1];
    head[21] = kProbeCap[2];
    head[22] = kProbeCap[3];
    gpuBufferFlush(p.table, d, 0, tableBytes);
    gpuBufferFlush(p.indices, d, 0, p.indices.size);

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = p.table.address + posByte;
    geom.geometry.triangles.vertexStride = sizeof(PackedPos);
    geom.geometry.triangles.maxVertex = p.triangles * 3u - 1u;
    geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geom.geometry.triangles.indexData.deviceAddress = p.indices.address;

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geom;
    const uint32_t prims = p.triangles;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        d.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &prims, &sizes);
    const VkDeviceSize scratchBytes = alignUp(sizes.buildScratchSize);
    if (!gpuBufferCreate(p.asBuf, d, sizes.accelerationStructureSize,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false)
        || !gpuBufferCreate(p.scratch, d, scratchBytes, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false)) {
        spdlog::error("radiance AS memory failed");
        return;
    }
    VkAccelerationStructureCreateInfoKHR create{};
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.buffer = p.asBuf.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(d.device, &create, nullptr, &p.blas) != VK_SUCCESS) {
        spdlog::error("radiance AS create failed");
        return;
    }
    build.dstAccelerationStructure = p.blas;
    build.scratchData.deviceAddress = p.scratch.address;
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = p.triangles;
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
    if (!gpuTaskBegin(p.buildTask, d)) {
        spdlog::error("radiance build begin failed");
        return;
    }
    const VkCommandBuffer cmd = p.buildTask.cmd;
    bufferBarrierHostToAccel(cmd, p.table.buffer);
    bufferBarrierHostToAccel(cmd, p.indices.buffer);
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rangePtr);
    rhiMemoryBarrier(cmd,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);

    const VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = p.blas,
    };
    const VkDeviceAddress addr = vkGetAccelerationStructureDeviceAddressKHR(d.device, &addrInfo);
    std::memcpy(p.table.mapped, &addr, sizeof(addr));
    gpuBufferFlush(p.table, d, 0, 16);
    if (!heapWriteStorage(heaps, d, HeapBuf::Rc, p.table)) {
        spdlog::error("radiance heap slot failed");
        return;
    }
    if (!gpuTaskEnd(p.buildTask)) {
        spdlog::error("radiance build end failed");
        return;
    }
    spdlog::info("radiance: {} triangles armed, frame stays on screen", p.triangles);
}

void radianceTraceArm(RadiancePass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent, uint32_t flight) {
    if (!p.geometry || p.trace.handle == VK_NULL_HANDLE || p.merge.handle == VK_NULL_HANDLE
        || p.irradiance.handle == VK_NULL_HANDLE || p.pushLayout == VK_NULL_HANDLE
        || p.posByte <= kRcHeader || !d.caps.rayQuery || deviceLost(d)) {
        return;
    }
    if (p.traceTask.stage != 0) {
        return;
    }
    if (!gpuTaskBegin(p.traceTask, d)) {
        spdlog::error("radiance trace begin failed");
        return;
    }
    auto* head = static_cast<uint32_t*>(p.table.mapped);
    const uint32_t n = head[27] + 1u;
    head[27] = n;
    const float jx = static_cast<float>(n & 1023u) / 1024.0f;
    const float jy = static_cast<float>((n * 17u) & 1023u) / 1024.0f;
    std::memcpy(head + 4, &jx, sizeof(float));
    std::memcpy(head + 5, &jy, sizeof(float));
    head[23] = 0;
    head[24] = 0;
    head[25] = 0;
    head[26] = 0;
    gpuBufferFlush(p.table, d, 0, kRcHeader);
    const VkDeviceSize cacheBytes = static_cast<VkDeviceSize>(p.posByte - kRcHeader);
    const VkCommandBuffer cmd = p.traceTask.cmd;
    heapBind(cmd, heaps, flight);
    bufferBarrierToCompute(cmd, p.table.buffer, 0, kRcHeader,
        VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    bufferBarrierToTransfer(cmd, p.table.buffer, kRcHeader, cacheBytes,
        VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdFillBuffer(cmd, p.table.buffer, kRcHeader, cacheBytes, 0);
    bufferBarrierTransferToCompute(cmd, p.table.buffer, kRcHeader, cacheBytes);
    dispatchCompute2D(cmd, p.trace, extent);
    struct MergePush {
        uint32_t cascade;
        uint32_t lod;
    };
    for (int c = 3; c >= 0; --c) {
        rhiBufferBarrier(cmd, p.table.buffer, kRcHeader, cacheBytes,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const MergePush push{static_cast<uint32_t>(c), 0xFFFFFFFFu};
        vkCmdPushConstants(cmd, p.pushLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        cmdBindCompute(cmd, p.merge.handle);
        const uint32_t threads = kProbeCap[c] * kDirCount[c];
        vkCmdDispatch(cmd, (threads + 63u) / 64u, 1u, 1u);
    }
    rhiBufferBarrier(cmd, p.table.buffer, kRcHeader, cacheBytes,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    cmdBindCompute(cmd, p.irradiance.handle);
    vkCmdDispatch(cmd, (kProbeCap[0] + 63u) / 64u, 1u, 1u);
    if (!gpuTaskEnd(p.traceTask)) {
        spdlog::error("radiance trace end failed");
    }
}

void radianceRecord(VkCommandBuffer cmd, VkExtent2D extent, RadiancePass& p, bool freeze) {
    (void)freeze;
    if (!p.hashLive || p.apply.handle == VK_NULL_HANDLE) {
        return;
    }
    dispatchCompute2D(cmd, p.apply, extent);
    p.hashDrawn = true;
}

} // namespace burnhope
