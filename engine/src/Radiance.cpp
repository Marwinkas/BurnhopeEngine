#include "render/Radiance.hpp"

#include "render/HeapBind.hpp"
#include "render/PassBufferSync.hpp"
#include "render/RenderDefs.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace burnhope {

namespace {

constexpr uint32_t kCascadeN = 4;
constexpr uint32_t kHashCap = 262144;
constexpr uint32_t kHashStride = 16;
constexpr uint32_t kProbeCap[4] = {65536u, 16384u, 4096u, 1024u};
constexpr uint32_t kMetaStride = 64;
constexpr uint32_t kDirCount[4] = {32u, 128u, 512u, 2048u};
constexpr uint32_t kDirStride = 32;
constexpr uint32_t kMergedStride = 16;
constexpr uint32_t kIrrStride = 36u * 16u;
constexpr uint32_t kGlobalBytes = 256;
constexpr uint32_t kSliceHead = 8192;
constexpr uint32_t kScanThreads = 512;
constexpr uint32_t kScanBlocks = 128; 
constexpr uint32_t kBlockSum = 16;
constexpr uint32_t kBlockExc = kBlockSum + kScanBlocks * 4u;
constexpr uint32_t kArgsRel = kBlockExc + kScanBlocks * 4u;

static_assert(kHashCap != 0u && (kHashCap & (kHashCap - 1u)) == 0u);
static_assert(kProbeCap[0] / kScanThreads == kScanBlocks);
static_assert(kArgsRel + 6u * 16u <= kSliceHead);
static_assert(kDirCount[0] == 32u && kDirCount[1] == 128u && kDirCount[2] == 512u && kDirCount[3] == 2048u);
static_assert(sizeof(RcDirAccum) == kDirStride);
static_assert(sizeof(RcProbeData) == kMetaStride);
static_assert(sizeof(RcMerged) == kMergedStride);

constexpr uint32_t rcMortonPack(uint32_t x, uint32_t y) {
    uint32_t code = 0;
    code |= (x & 1u) << 0u;
    code |= (y & 1u) << 1u;
    code |= (x & 2u) << 1u;
    code |= (y & 2u) << 2u;
    code |= (x & 4u) << 2u;
    return code;
}

constexpr uint32_t rcMortonX(uint32_t code) {
    return (code & 1u) | ((code & 4u) >> 1u) | ((code & 16u) >> 2u);
}

constexpr uint32_t rcMortonY(uint32_t code) {
    return ((code & 2u) >> 1u) | ((code & 8u) >> 2u);
}

constexpr bool rcMortonRoundtrip() {
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 8u; ++x) {
            const uint32_t code = rcMortonPack(x, y);
            if (code >= 32u || rcMortonX(code) != x || rcMortonY(code) != y) {
                return false;
            }
        }
    }
    return true;
}

constexpr uint32_t rcHier(uint32_t u, uint32_t v, uint32_t cascade) {
    const uint32_t coarse = rcMortonPack(u >> cascade, v >> cascade);
    uint32_t fine = 0;
    if (cascade > 0u) {
        fine |= ((v & 1u) << 1u) | (u & 1u);
    }
    if (cascade > 1u) {
        fine |= ((((v >> 1u) & 1u) << 1u) | ((u >> 1u) & 1u)) << 2u;
    }
    if (cascade > 2u) {
        fine |= ((((v >> 2u) & 1u) << 1u) | ((u >> 2u) & 1u)) << 4u;
    }
    return (coarse << (cascade * 2u)) | fine;
}

constexpr bool rcHierChildren(uint32_t cascade, uint32_t uCount, uint32_t vCount) {
    for (uint32_t v = 0; v < vCount; ++v) {
        for (uint32_t u = 0; u < uCount; ++u) {
            const uint32_t parent = rcHier(u, v, cascade);
            for (uint32_t dv = 0; dv < 2u; ++dv) {
                for (uint32_t du = 0; du < 2u; ++du) {
                    const uint32_t quad = (dv << 1u) | du;
                    const uint32_t child = rcHier(u * 2u + du, v * 2u + dv, cascade + 1u);
                    if (child != parent * 4u + quad) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

static_assert(rcMortonRoundtrip());
static_assert(rcHierChildren(0u, 8u, 4u));
static_assert(rcHierChildren(1u, 16u, 8u));

struct alignas(16) RcGlobal {
    uint32_t sliceStride = 0;
    uint32_t sliceBytes = 0;
    uint32_t hashCap = 0;
    uint32_t posByte = 0;
    uint32_t hashRel[4]{};
    uint32_t metaRel[4]{};
    uint32_t dirRel[4]{};
    uint32_t mergedRel[4]{};
    uint32_t irrRel = 0;
    uint32_t prefixRel = 0;
    uint32_t claimRel = 0;
    uint32_t probeCap[4]{};
    uint32_t headerPad = 0;
    uint32_t blasLo = 0;
    uint32_t blasHi = 0;
    uint32_t hashStride = 0;
    uint32_t metaStride = 0;
    uint32_t dirStride = 0;
    uint32_t mergedStride = 0;
    uint32_t irrStride = 0;
    uint32_t dirCount[4]{};
    uint32_t tail[25]{};
};
static_assert(sizeof(RcGlobal) == kGlobalBytes);
static_assert(offsetof(RcGlobal, hashCap) == 8);
static_assert(offsetof(RcGlobal, posByte) == 12);
static_assert(offsetof(RcGlobal, hashRel) == 16);
static_assert(offsetof(RcGlobal, metaRel) == 32);
static_assert(offsetof(RcGlobal, dirRel) == 48);
static_assert(offsetof(RcGlobal, mergedRel) == 64);
static_assert(offsetof(RcGlobal, irrRel) == 80);
static_assert(offsetof(RcGlobal, prefixRel) == 84);
static_assert(offsetof(RcGlobal, claimRel) == 88);
static_assert(offsetof(RcGlobal, probeCap) == 92);
static_assert(offsetof(RcGlobal, headerPad) == 108);
static_assert(offsetof(RcGlobal, tail) == 156);
static_assert(offsetof(RcGlobal, blasLo) == 112);
static_assert(offsetof(RcGlobal, blasHi) == 116);
static_assert(offsetof(RcGlobal, hashStride) == 120);
static_assert(offsetof(RcGlobal, dirCount) == 140);

struct RcLayout {
    uint32_t hashRel[4]{};
    uint32_t metaRel[4]{};
    uint32_t dirRel[4]{};
    uint32_t mergedRel[4]{};
    uint32_t irrRel = 0;
    uint32_t prefixRel = 0;
    uint32_t claimRel = 0;
    uint32_t sliceStride = 0;
    uint32_t clearBytes = 0;
    uint32_t posByte = 0;
};

uint32_t align256(uint32_t value) {
    return (value + 255u) & ~255u;
}

RcLayout rcLayout() {
    RcLayout layout{};
    uint32_t cursor = kSliceHead;
    for (uint32_t c = 0; c < kCascadeN; ++c) {
        layout.hashRel[c] = cursor;
        cursor += kHashCap * kHashStride;
    }
    for (uint32_t c = 0; c < kCascadeN; ++c) {
        layout.metaRel[c] = cursor;
        cursor += kProbeCap[c] * kMetaStride;
    }
    layout.claimRel = cursor;
    cursor += kProbeCap[0] * 4u;
    layout.prefixRel = cursor;
    cursor += kProbeCap[0] * 4u;
    for (uint32_t c = 0; c < kCascadeN; ++c) {
        layout.dirRel[c] = cursor;
        cursor += kProbeCap[c] * kDirCount[c] * kDirStride;
    }
    layout.clearBytes = cursor;
    for (uint32_t c = 0; c < kCascadeN; ++c) {
        layout.mergedRel[c] = cursor;
        cursor += kProbeCap[c] * kDirCount[c] * kMergedStride;
    }
    layout.irrRel = cursor;
    cursor += kProbeCap[0] * kIrrStride;
    layout.sliceStride = align256(cursor);
    layout.posByte = kGlobalBytes + layout.sliceStride * 2u;
    return layout;
}

struct alignas(16) RcTriVert {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float v = 0.0f;
    uint32_t material = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};
static_assert(sizeof(RcTriVert) == 48);
static_assert(alignof(RcTriVert) == 16);

void transform(const float* m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[1] * y + m[2] * z + m[3];
    oy = m[4] * x + m[5] * y + m[6] * z + m[7];
    oz = m[8] * x + m[9] * y + m[10] * z + m[11];
}

void transformNormal(const float* m, float x, float y, float z, float& ox, float& oy, float& oz) {
    const float c00 = m[5] * m[10] - m[6] * m[9];
    const float c01 = m[6] * m[8] - m[4] * m[10];
    const float c02 = m[4] * m[9] - m[5] * m[8];
    const float c10 = m[2] * m[9] - m[1] * m[10];
    const float c11 = m[0] * m[10] - m[2] * m[8];
    const float c12 = m[1] * m[8] - m[0] * m[9];
    const float c20 = m[1] * m[6] - m[2] * m[5];
    const float c21 = m[2] * m[4] - m[0] * m[6];
    const float c22 = m[0] * m[5] - m[1] * m[4];
    ox = c00 * x + c01 * y + c02 * z;
    oy = c10 * x + c11 * y + c12 * z;
    oz = c20 * x + c21 * y + c22 * z;
    const float len = std::sqrt(ox * ox + oy * oy + oz * oz);
    if (len > 1.0e-8f) {
        ox /= len;
        oy /= len;
        oz /= len;
    } else {
        ox = 0.0f;
        oy = 1.0f;
        oz = 0.0f;
    }
}

bool rcTriLive(const GpuMeshlet& mlet, const uint32_t* indices, uint32_t indexCount, uint32_t vertCount, uint32_t tri) {
    const uint64_t at = static_cast<uint64_t>(mlet.indexOffset) + static_cast<uint64_t>(tri) * 3u;
    if (mlet.vertexCount == 0 || at + 2u >= indexCount) {
        return false;
    }
    for (uint32_t k = 0; k < 3u; ++k) {
        const uint32_t local = indices[at + k];
        if (local >= mlet.vertexCount || static_cast<uint64_t>(mlet.vertexOffset) + local >= vertCount) {
            return false;
        }
    }
    return true;
}

void rcFreeGeometry(RadiancePass& p, Device& d) {
    gpuTaskDestroy(p.buildTask, d);
    if (p.tlas != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(d.device, p.tlas, nullptr);
    }
    if (p.blas != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(d.device, p.blas, nullptr);
    }
    p.tlas = VK_NULL_HANDLE;
    p.blas = VK_NULL_HANDLE;
    gpuBufferDestroy(p.table, d);
    gpuBufferDestroy(p.staging, d);
    gpuBufferDestroy(p.indices, d);
    gpuBufferDestroy(p.scratch, d);
    gpuBufferDestroy(p.asBuf, d);
    gpuBufferDestroy(p.tlasScratch, d);
    gpuBufferDestroy(p.tlasBuf, d);
    gpuBufferDestroy(p.tlasInst, d);
    p.posByte = 0;
    p.triangles = 0;
    p.sliceStride = 0;
    p.clearBytes = 0;
    p.geometry = false;
    p.hashLive = false;
    p.bakedMask = 0;
    p.bakeSpacing = -1.0f;
    p.bakeT0 = -1.0f;
    p.bakeSun = -1.0f;
    p.bakeBounce = -1.0f;
    p.bakeSunX = -1.0f;
    p.bakeSunY = -1.0f;
    p.bakeSunZ = -1.0f;
}

bool rcShader(Device& d, const char* path, const PassBindings& bind, ShaderExt& out) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
        .pushBytes = sizeof(RcPushConstants),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    return shaderCreate(d, desc, out);
}

void rcBind(VkCommandBuffer cmd, const ShaderExt& shader, const RcPushConstants& push) {
    cmdBindCompute(cmd, shader.handle);
    vkCmdPushConstants(cmd, VK_NULL_HANDLE, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
}

void rcBarrierCompute(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
    rhiBufferBarrier(cmd, buffer, offset, size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void rcBarrierIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset) {
    rhiBufferBarrier(cmd, buffer, offset, 16,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

void rcMergeOct(VkCommandBuffer cmd, RadiancePass& p, RcPushConstants push, VkDeviceSize slice) {
    for (int cascade = 3; cascade >= 0; --cascade) {
        const VkDeviceSize args = slice + kArgsRel + static_cast<VkDeviceSize>(cascade) * 16u;
        rcBarrierIndirect(cmd, p.table.buffer, args);
        push.cascadeIndex = static_cast<uint32_t>(cascade);
        rcBind(cmd, p.merge, push);
        vkCmdDispatchIndirect(cmd, p.table.buffer, args);
        rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);
    }
    const VkDeviceSize octArgs = slice + kArgsRel + 4u * 16u;
    rcBarrierIndirect(cmd, p.table.buffer, octArgs);
    push.cascadeIndex = 0;
    rcBind(cmd, p.octahedral, push);
    vkCmdDispatchIndirect(cmd, p.table.buffer, octArgs);
    rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);
}

} // namespace

void radianceDrop(RadiancePass& p, Device& d) {
    rcFreeGeometry(p, d);
    p.pause = 0;
}

bool radianceCreate(RadiancePass& p, Device& d, const DescriptorHeaps& heaps) {
    radianceDestroy(p, d);
    if (!d.caps.rayQuery) {
        spdlog::info("radiance: ray query is off");
        return true;
    }
    PassBindings alloc{heaps};
    alloc.ubo(0, HeapBuf::Frame);
    alloc.sampledImg(1, HeapImg::Vis);
    alloc.sampledImg(2, HeapImg::Depth);
    alloc.storageImg(5, HeapImg::Surf1);
    alloc.storageBuf(30, HeapBuf::Rc, false);
    PassBindings prefix{heaps};
    prefix.storageBuf(30, HeapBuf::Rc, false);
    PassBindings trace{heaps};
    trace.ubo(0, HeapBuf::Frame);
    trace.sun();
    trace.sampledImg(1, HeapImg::Vis);
    trace.sampledImg(2, HeapImg::Depth);
    trace.sampledImg(3, HeapImg::HdrASampled);
    trace.storageImg(4, HeapImg::Surf0);
    trace.storageImg(5, HeapImg::Surf1);
    trace.sampledImg(12, HeapImg::Textures);
    trace.storageBuf(14, HeapBuf::Materials);
    trace.sampler(24, HeapSamp::Wrap);
    trace.storageBuf(30, HeapBuf::Rc, false);
    trace.knob(50, HeapBuf::Flags);
    trace.knob(54, HeapBuf::RcParams);
    PassBindings merge{heaps};
    merge.storageBuf(30, HeapBuf::Rc, false);
    PassBindings oct{heaps};
    oct.storageBuf(30, HeapBuf::Rc, false);
    PassBindings shade{heaps};
    shade.ubo(0, HeapBuf::Frame);
    shade.sun();
    shade.sampledImg(1, HeapImg::Vis);
    shade.sampledImg(2, HeapImg::Depth);
    shade.storageImg(3, HeapImg::HdrAStorage);
    shade.storageImg(4, HeapImg::Surf0);
    shade.storageImg(5, HeapImg::Surf1);
    shade.storageBuf(30, HeapBuf::Rc, false);
    shade.sampledImg(32, HeapImg::Ao);
    shade.sampledImg(47, HeapImg::ContactSample);
    shade.knob(50, HeapBuf::Flags);
    shade.knob(54, HeapBuf::RcParams);
    shade.knob(55, HeapBuf::Shade);
    const bool ok = rcShader(d, BH_SHADER_DIR "/rc.alloc.comp.spv", alloc, p.alloc)
        && rcShader(d, BH_SHADER_DIR "/rc.prefix.comp.spv", prefix, p.prefix)
        && rcShader(d, BH_SHADER_DIR "/rc.trace.comp.spv", trace, p.trace)
        && rcShader(d, BH_SHADER_DIR "/rc.merge.comp.spv", merge, p.merge)
        && rcShader(d, BH_SHADER_DIR "/rc.oct.comp.spv", oct, p.octahedral)
        && rcShader(d, BH_SHADER_DIR "/rc.shade.comp.spv", shade, p.shade);
    if (!ok) {
        spdlog::error("radiance shaders failed");
        radianceDestroy(p, d);
        return false;
    }
    p.slots = kHashCap;
    if (!gpuBufferCreate(p.check, d, 256, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        spdlog::error("radiance check buffer failed");
    }
    return true;
}

void radianceDestroy(RadiancePass& p, Device& d) {
    rcFreeGeometry(p, d);
    shaderDestroy(d, p.shade);
    shaderDestroy(d, p.octahedral);
    shaderDestroy(d, p.merge);
    shaderDestroy(d, p.trace);
    shaderDestroy(d, p.prefix);
    shaderDestroy(d, p.alloc);
    gpuBufferDestroy(p.check, d);
    p.temporal = 0;
    p.push = {};
}

void radianceKick(RadiancePass& p, Device& d) {
    if (gpuTaskDone(p.buildTask, d)) {
        p.geometry = true;
        gpuBufferDestroy(p.staging, d);
        return;
    }
    if (p.buildTask.stage != 1) {
        return;
    }
    if (!gpuTaskSubmit(p.buildTask, d)) {
        spdlog::error("radiance build submit failed");
        return;
    }
    spdlog::info("radiance build queued, {} triangles", p.triangles);
}

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
    uint32_t meshletCount) {
    if (p.geometry || p.buildTask.stage != 0 || !d.caps.rayQuery || p.trace.handle == VK_NULL_HANDLE) {
        return;
    }
    if (verts == nullptr || indices == nullptr || instances == nullptr || meshlets == nullptr || instanceCount == 0
        || vertCount == 0 || indexCount < 3u || meshletCount == 0) {
        return;
    }
    uint64_t triCount = 0;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const uint32_t mesh = instances[i].meshId;
        if (mesh >= meshletCount) {
            continue;
        }
        const GpuMeshlet& mlet = meshlets[mesh];
        for (uint32_t t = 0; t < mlet.triangleCount; ++t) {
            if (rcTriLive(mlet, indices, indexCount, vertCount, t)) {
                triCount += 1u;
            }
        }
    }
    if (triCount == 0 || triCount > 0xffffffffu) {
        spdlog::error("radiance: no triangles");
        return;
    }
    std::vector<RcTriVert> positions(static_cast<size_t>(triCount) * 3u);
    std::vector<uint32_t> tris(static_cast<size_t>(triCount) * 3u);
    uint32_t written = 0;
    float loX = 1.0e30f;
    float loY = 1.0e30f;
    float loZ = 1.0e30f;
    float hiX = -1.0e30f;
    float hiY = -1.0e30f;
    float hiZ = -1.0e30f;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const uint32_t mesh = instances[i].meshId;
        if (mesh >= meshletCount) {
            continue;
        }
        const GpuMeshlet& mlet = meshlets[mesh];
        const float* m = instances[i].world;
        for (uint32_t t = 0; t < mlet.triangleCount; ++t) {
            if (!rcTriLive(mlet, indices, indexCount, vertCount, t)) {
                continue;
            }
            const uint32_t at = mlet.indexOffset + t * 3u;
            const uint32_t base = written * 3u;
            for (uint32_t k = 0; k < 3u; ++k) {
                const GpuVertex& v = verts[mlet.vertexOffset + indices[at + k]];
                RcTriVert out{};
                transform(m, v.px, v.py, v.pz, out.x, out.y, out.z);
                loX = std::min(loX, out.x);
                loY = std::min(loY, out.y);
                loZ = std::min(loZ, out.z);
                hiX = std::max(hiX, out.x);
                hiY = std::max(hiY, out.y);
                hiZ = std::max(hiZ, out.z);
                transformNormal(m, v.nx, v.ny, v.nz, out.nx, out.ny, out.nz);
                out.u = v.pad0;
                out.v = v.pad1;
                out.material = instances[i].materialId;
                positions[base + k] = out;
                tris[base + k] = base + k;
            }
            written += 1u;
        }
    }
    p.triangles = written;
    if (p.triangles == 0) {
        spdlog::error("radiance: no triangles");
        return;
    }
    const RcLayout layout = rcLayout();
    const VkDeviceSize posBytes = positions.size() * sizeof(RcTriVert);
    const VkDeviceSize tableBytes = layout.posByte + posBytes;
    const VkBufferUsageFlags tableUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    const VkBufferUsageFlags indexUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!gpuBufferCreate(p.table, d, tableBytes, tableUsage, false)
        || !gpuBufferCreate(p.staging, d, kGlobalBytes + posBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)
        || !gpuBufferCreate(p.indices, d, tris.size() * sizeof(uint32_t), indexUsage, true)) {
        spdlog::error("radiance buffers failed");
        rcFreeGeometry(p, d);
        return;
    }
    RcGlobal header{};
    header.sliceStride = layout.sliceStride;
    header.sliceBytes = layout.sliceStride;
    header.hashCap = kHashCap;
    header.posByte = layout.posByte;
    for (uint32_t c = 0; c < kCascadeN; ++c) {
        header.hashRel[c] = layout.hashRel[c];
        header.metaRel[c] = layout.metaRel[c];
        header.dirRel[c] = layout.dirRel[c];
        header.mergedRel[c] = layout.mergedRel[c];
        header.probeCap[c] = kProbeCap[c];
        header.dirCount[c] = kDirCount[c];
    }
    header.irrRel = layout.irrRel;
    header.prefixRel = layout.prefixRel;
    header.claimRel = layout.claimRel;
    header.headerPad = kScanBlocks;
    header.tail[0] = kBlockSum;
    header.tail[1] = kBlockExc;
    header.tail[2] = kArgsRel;
    header.tail[3] = p.triangles;
    header.hashStride = kHashStride;
    header.metaStride = kMetaStride;
    header.dirStride = kDirStride;
    header.mergedStride = kMergedStride;
    header.irrStride = kIrrStride;
    std::memcpy(p.staging.mapped, &header, sizeof(header));
    std::memcpy(static_cast<std::byte*>(p.staging.mapped) + kGlobalBytes, positions.data(), posBytes);
    std::memcpy(p.indices.mapped, tris.data(), tris.size() * sizeof(uint32_t));

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = p.table.address + layout.posByte;
    geom.geometry.triangles.vertexStride = sizeof(RcTriVert);
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
    vkGetAccelerationStructureBuildSizesKHR(d.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &prims, &sizes);
    if (!gpuBufferCreate(p.asBuf, d, sizes.accelerationStructureSize,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false)
        || !gpuBufferCreate(p.scratch, d, alignUp(sizes.buildScratchSize),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false)) {
        spdlog::error("radiance AS memory failed");
        rcFreeGeometry(p, d);
        return;
    }
    VkAccelerationStructureCreateInfoKHR create{};
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.buffer = p.asBuf.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(d.device, &create, nullptr, &p.blas) != VK_SUCCESS) {
        spdlog::error("radiance AS create failed");
        rcFreeGeometry(p, d);
        return;
    }
    const VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = p.blas,
    };
    const VkDeviceAddress blasAddr = vkGetAccelerationStructureDeviceAddressKHR(d.device, &addrInfo);
    const VkBufferUsageFlags instUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!gpuBufferCreate(p.tlasInst, d, sizeof(VkAccelerationStructureInstanceKHR), instUsage, true)) {
        spdlog::error("radiance TLAS instance failed");
        rcFreeGeometry(p, d);
        return;
    }
    VkAccelerationStructureInstanceKHR inst{};
    inst.transform.matrix[0][0] = 1.0f;
    inst.transform.matrix[1][1] = 1.0f;
    inst.transform.matrix[2][2] = 1.0f;
    inst.mask = 0xff;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
        | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    inst.accelerationStructureReference = blasAddr;
    std::memcpy(p.tlasInst.mapped, &inst, sizeof(inst));
    gpuBufferFlush(p.tlasInst, d, 0, sizeof(inst));
    VkAccelerationStructureGeometryKHR tgeom{};
    tgeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tgeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tgeom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tgeom.geometry.instances.arrayOfPointers = VK_FALSE;
    tgeom.geometry.instances.data.deviceAddress = p.tlasInst.address;
    VkAccelerationStructureBuildGeometryInfoKHR tbuild{};
    tbuild.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tbuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tbuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tbuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tbuild.geometryCount = 1;
    tbuild.pGeometries = &tgeom;
    const uint32_t oneInst = 1u;
    VkAccelerationStructureBuildSizesInfoKHR tsizes{};
    tsizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        d.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tbuild, &oneInst, &tsizes);
    if (tsizes.accelerationStructureSize == 0
        || !gpuBufferCreate(p.tlasBuf, d, tsizes.accelerationStructureSize,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false)
        || !gpuBufferCreate(p.tlasScratch, d, alignUp(std::max(tsizes.buildScratchSize, VkDeviceSize{256})),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false)) {
        spdlog::error("radiance TLAS memory failed");
        rcFreeGeometry(p, d);
        return;
    }
    VkAccelerationStructureCreateInfoKHR tcreate{};
    tcreate.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tcreate.buffer = p.tlasBuf.buffer;
    tcreate.size = tsizes.accelerationStructureSize;
    tcreate.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(d.device, &tcreate, nullptr, &p.tlas) != VK_SUCCESS) {
        spdlog::error("radiance TLAS create failed");
        rcFreeGeometry(p, d);
        return;
    }
    const VkAccelerationStructureDeviceAddressInfoKHR taddr{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = p.tlas,
    };
    const VkDeviceAddress tlasAddr = vkGetAccelerationStructureDeviceAddressKHR(d.device, &taddr);
    std::memcpy(&header.blasLo, &tlasAddr, sizeof(tlasAddr));
    std::memcpy(p.staging.mapped, &header, sizeof(header));
    gpuBufferFlush(p.staging, d, 0, p.staging.size);
    gpuBufferFlush(p.indices, d, 0, p.indices.size);
    build.dstAccelerationStructure = p.blas;
    build.scratchData.deviceAddress = p.scratch.address;
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = p.triangles;
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
    if (!gpuTaskBegin(p.buildTask, d)) {
        spdlog::error("radiance build begin failed");
        rcFreeGeometry(p, d);
        return;
    }
    const VkCommandBuffer cmd = p.buildTask.cmd;
    rhiBufferBarrier(cmd, p.staging.buffer, 0, p.staging.size,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    bufferBarrierHostToAccel(cmd, p.indices.buffer);
    const VkBufferCopy headCopy{.srcOffset = 0, .dstOffset = 0, .size = kGlobalBytes};
    const VkBufferCopy posCopy{.srcOffset = kGlobalBytes, .dstOffset = layout.posByte, .size = posBytes};
    vkCmdCopyBuffer(cmd, p.staging.buffer, p.table.buffer, 1, &headCopy);
    vkCmdCopyBuffer(cmd, p.staging.buffer, p.table.buffer, 1, &posCopy);
    rhiBufferBarrier(cmd, p.table.buffer, layout.posByte, posBytes,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rangePtr);
    rhiMemoryBarrier(cmd,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    bufferBarrierHostToAccel(cmd, p.tlasInst.buffer);
    tbuild.dstAccelerationStructure = p.tlas;
    tbuild.scratchData.deviceAddress = p.tlasScratch.address;
    VkAccelerationStructureBuildRangeInfoKHR trange{};
    trange.primitiveCount = 1u;
    const VkAccelerationStructureBuildRangeInfoKHR* trangePtr = &trange;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tbuild, &trangePtr);
    rhiMemoryBarrier(cmd,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    if (!gpuTaskEnd(p.buildTask)) {
        spdlog::error("radiance build end failed");
        rcFreeGeometry(p, d);
        return;
    }
    if (!heapWriteStorage(heaps, d, HeapBuf::Rc, p.table)) {
        spdlog::error("radiance heap slot failed");
        rcFreeGeometry(p, d);
        return;
    }
    p.posByte = layout.posByte;
    p.sliceStride = layout.sliceStride;
    p.clearBytes = layout.clearBytes;
    spdlog::info(
        "radiance: {} triangles, slice {} bytes, tlas {:x}, aabb {:.1f} {:.1f} {:.1f} .. {:.1f} {:.1f} {:.1f}",
        p.triangles, layout.sliceStride, static_cast<unsigned long long>(tlasAddr), loX, loY, loZ, hiX, hiY, hiZ);
}

struct RcCheck {
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t zeroLe = 0;
    uint32_t leSum = 0;
    uint32_t visSum = 0;
    uint32_t ndlSum = 0;
    float albedo = 0.0f;
    float vis = 0.0f;
    float ndl = 0.0f;
    float metal = 0.0f;
    uint32_t flag = 0;
    uint32_t octFlag = 0;
    float octIrr = 0.0f;
    uint32_t probes = 0;
    uint32_t hitTSum = 0;
    uint32_t overflow = 0;
    uint32_t c1 = 0;
    uint32_t c2 = 0;
    uint32_t c3 = 0;
    uint32_t padC = 0;
};
static_assert(sizeof(RcCheck) == 80);

constexpr VkDeviceSize kCheckOff = 176;
constexpr VkDeviceSize kCheckBytes = 80;

void rcReadCheck(RadiancePass& p, Device& d, uint32_t flight, float space, float reach, float rays, float gain, float sunScale, float bounce) {
    const bool changed = p.logSpace != space || p.logReach != reach || p.logRays != rays || p.logGain != gain || p.logSun != sunScale || p.logBounce != bounce;
    p.logSpace = space;
    p.logReach = reach;
    p.logRays = rays;
    p.logGain = gain;
    p.logSun = sunScale;
    p.logBounce = bounce;
    if (changed) {
        spdlog::info(
            "radiance params space {:.3f} reach {:.1f} rays {:.0f} gain {:.2f} sun {:.2f} bounce {:.2f}",
            space, reach, rays, gain, sunScale, bounce);
    }
    if (p.check.mapped == nullptr || p.temporal < 2u) {
        return;
    }
    if (!changed && p.temporal > 12u && (p.temporal % 10u) != 0u) {
        return;
    }
    const VkDeviceSize slot = static_cast<VkDeviceSize>(flight & 1u) * kCheckBytes;
    gpuBufferInvalidate(p.check, d, slot, kCheckBytes);
    RcCheck c{};
    std::memcpy(&c, static_cast<const std::byte*>(p.check.mapped) + slot, sizeof(c));
    const float hitN = c.hits > 0u ? static_cast<float>(c.hits) : 1.0f;
    spdlog::info(
        "radiance check space {:.3f} reach {:.1f} rays {:.0f} gain {:.2f} sun {:.2f} bounce {:.2f} hit {} miss {} zero {} le {:.3f} vis {:.2f} ndl {:.2f} dist {:.2f} c0 {} c1 {} c2 {} c3 {} over {} oct {} irr {:.3f} sample {:.2f} {:.2f} {:.2f} {:.2f}",
        space, reach, rays, gain, sunScale, bounce,
        c.hits, c.misses, c.zeroLe,
        static_cast<float>(c.leSum) / 256.0f / hitN,
        static_cast<float>(c.visSum) / 256.0f / hitN,
        static_cast<float>(c.ndlSum) / 256.0f / hitN,
        static_cast<float>(c.hitTSum) / 256.0f / hitN,
        c.probes, c.c1, c.c2, c.c3, c.overflow, c.octFlag, c.octIrr,
        c.albedo, c.vis, c.ndl, c.metal);
}

void rcClearCheck(VkCommandBuffer cmd, VkBuffer table) {
    vkCmdFillBuffer(cmd, table, kCheckOff, 80, 0);
    rhiBufferBarrier(cmd, table, kCheckOff, 80,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void rcCopyCheck(VkCommandBuffer cmd, VkBuffer table, VkBuffer dst, uint32_t flight) {
    const VkDeviceSize slot = static_cast<VkDeviceSize>(flight & 1u) * kCheckBytes;
    rhiBufferBarrier(cmd, table, kCheckOff, kCheckBytes,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferCopy copy0{.srcOffset = kCheckOff, .dstOffset = 0, .size = kCheckBytes};
    const VkBufferCopy copy1{.srcOffset = kCheckOff, .dstOffset = kCheckBytes, .size = kCheckBytes};
    vkCmdCopyBuffer(cmd, table, dst, 1, &copy0);
    vkCmdCopyBuffer(cmd, table, dst, 1, &copy1);
    rhiBufferBarrier(cmd, dst, 0, kCheckBytes * 2u,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    (void)slot;
}

void rcPrefixScan(VkCommandBuffer cmd, RadiancePass& p, RcPushConstants push, VkDeviceSize slice) {
    push.cascadeIndex = 0;
    rcBind(cmd, p.prefix, push);
    vkCmdDispatch(cmd, kScanBlocks, 1u, 1u);
    rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);
    push.cascadeIndex = 1;
    rcBind(cmd, p.prefix, push);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
    rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);
    push.cascadeIndex = 2;
    rcBind(cmd, p.prefix, push);
    vkCmdDispatch(cmd, kScanBlocks, 1u, 1u);
    rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);
}
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
    VkCommandBuffer cmd) {
    (void)extent;
    if (cmd == VK_NULL_HANDLE || p.pause != 0 || !p.geometry || p.alloc.handle == VK_NULL_HANDLE || p.prefix.handle == VK_NULL_HANDLE
        || p.trace.handle == VK_NULL_HANDLE || p.merge.handle == VK_NULL_HANDLE || p.octahedral.handle == VK_NULL_HANDLE
        || p.shade.handle == VK_NULL_HANDLE || p.sliceStride == 0 || p.clearBytes == 0 || p.clearBytes > p.sliceStride
        || !d.caps.rayQuery || deviceLost(d) || p.triangles == 0) {
        p.hashLive = false;
        return;
    }
    const float cell = std::max(spacing, 0.1f);
    const float reach = std::max(maxDist, 0.5f);
    const uint32_t rayMult = std::clamp(static_cast<uint32_t>(std::round(rays)), 1u, 8u);
    const float raysF = static_cast<float>(rayMult * 32u);
    const float sx = std::round(sunX * 100.0f) * 0.01f;
    const float sy = std::round(sunY * 100.0f) * 0.01f;
    const float sz = std::round(sunZ * 100.0f) * 0.01f;
    const uint32_t flightBit = flight & 1u;

    p.bakeSpacing = cell;
    p.bakeT0 = reach;
    p.bakeSun = sunScale;
    p.bakeBounce = bounce;
    p.bakeSunX = sx;
    p.bakeSunY = sy;
    p.bakeSunZ = sz;

    p.push = RcPushConstants{
        .cascadeIndex = rayMult,
        .baseSpacing = cell,
        .baseLength = reach,
        .frameIndex = flightBit,
    };

    p.temporal += 1u;
    rcReadCheck(p, d, flight, cell, reach, raysF, intensity, sunScale, bounce);
    const VkDeviceSize slice = kGlobalBytes + static_cast<VkDeviceSize>(flightBit) * p.sliceStride;
    heapBind(cmd, heaps, flight);

    rhiBufferBarrier(cmd, p.table.buffer, slice, p.clearBytes,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cmd, p.table.buffer, slice, p.clearBytes, 0);
    rhiBufferBarrier(cmd, p.table.buffer, slice, p.sliceStride,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    if (flightBit == 0u) {
        rcClearCheck(cmd, p.table.buffer);
    }

    // 1. Аллокация разреженных зондов по видимым пикселям экрана (1D развёртка)
    // 1. Детерминированная 3-фазная аллокация зондов (без гонок записи P/N и без мерцания!)
    for (uint32_t stage = 0u; stage < 3u; ++stage) {
        RcPushConstants allocPush = p.push;
        allocPush.cascadeIndex = stage;
        rcBind(cmd, p.alloc, allocPush);
        vkCmdDispatch(cmd, 4096u, 1u, 1u);
        rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);
    }

    // 2. Префиксная сумма и подготовка аргументов Indirect Dispatch
    rcPrefixScan(cmd, p, p.push, slice);

    // 3. Трассировка лучей и Ray Splitting
    const VkDeviceSize traceArgs = slice + kArgsRel + 5u * 16u;
    rcBarrierIndirect(cmd, p.table.buffer, traceArgs);
    rcBind(cmd, p.trace, p.push);
    vkCmdDispatchIndirect(cmd, p.table.buffer, traceArgs);
    rcBarrierCompute(cmd, p.table.buffer, slice, p.sliceStride);

    // 4. Слияние каскадов c3 -> c0 и свёртка в 6x6 октаэдр
    rcMergeOct(cmd, p, p.push, slice);

    if (flightBit == 0u && p.check.buffer != VK_NULL_HANDLE) {
        rcCopyCheck(cmd, p.table.buffer, p.check.buffer, flight);
    }
    p.bakedMask = static_cast<uint8_t>(p.bakedMask | (1u << flightBit));
    p.hashLive = true;
}
void radianceRecord(VkCommandBuffer cmd, VkExtent2D extent, RadiancePass& p, bool freeze) {
    (void)freeze;
    if (cmd == VK_NULL_HANDLE || !p.hashLive || p.shade.handle == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0) {
        return;
    }
    rcBind(cmd, p.shade, p.push);
    dispatch2D(cmd, extent, 8u);
}

} // namespace burnhope
