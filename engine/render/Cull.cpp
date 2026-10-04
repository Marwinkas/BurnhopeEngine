#include "render/Cull.hpp"
#include "render/HeapBind.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/PassShaderGroup.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif

#include <spdlog/spdlog.h>

namespace burnhope {

bool occlPassCreate(OcclPass& p, Device& d, const DescriptorHeaps& heaps) {
    occlPassDestroy(p, d);
    PassBindings b{heaps};
    b.ubo(0, HeapBuf::Frame);
    b.sampledImg(6, HeapImg::HiZSample);
    b.storageBuf(10, HeapBuf::Instances);
    b.storageBuf(11, HeapBuf::Visible, false);
    b.storageBuf(28, HeapBuf::Candidates, false);
    b.storageBuf(29, HeapBuf::Indirect, false);
    b.sampledImg(31, HeapImg::HiZSample1);
    b.storageBuf(9, HeapBuf::Meshlets);
    b.storageBuf(8, HeapBuf::Sectors);
    b.knob(50, HeapBuf::Flags);
    VkDescriptorSetAndBindingMappingEXT maps[10];
    for (uint32_t i = 0; i < b.raw.count; ++i) {
        maps[i] = b.raw.mappings[i];
    }
    const char* clearPath[2] = {BH_SHADER_DIR "/cull.clear.spv", BH_SHADER_DIR "/cull.clear1.spv"};
    const char* compactPath[2] = {BH_SHADER_DIR "/cull.comp.spv", BH_SHADER_DIR "/cull.comp1.spv"};
    const char* latePath[2] = {BH_SHADER_DIR "/cull.late.spv", BH_SHADER_DIR "/cull.late1.spv"};
    if (!createShaderArray(d, p.clear, clearPath, VK_SHADER_STAGE_COMPUTE_BIT, maps, 9u)
        || !createShaderArray(d, p.compact, compactPath, VK_SHADER_STAGE_COMPUTE_BIT, maps, 10u)
        || !createShaderArray(d, p.late, latePath, VK_SHADER_STAGE_COMPUTE_BIT, maps, 10u)) {
        spdlog::error("occlusion cull shader failed");
        occlPassDestroy(p, d);
        return false;
    }
    return true;
}

void occlPassDestroy(OcclPass& p, Device& d) {
    shaderDestroy(d, p.clear[0]);
    shaderDestroy(d, p.clear[1]);
    shaderDestroy(d, p.compact[0]);
    shaderDestroy(d, p.compact[1]);
    shaderDestroy(d, p.late[0]);
    shaderDestroy(d, p.late[1]);
}

void occlPassRecord(VkCommandBuffer cmd, const OcclPass& p, const GpuBuffer& indirect, const GpuBuffer& flags, const GpuBuffer& visible, uint32_t count, bool late, uint32_t flight) {
    if (!late) {
        bufferBarrierToCompute(cmd, indirect.buffer, 0, indirect.size, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);
        bufferBarrierToCompute(cmd, flags.buffer, 0, flags.size, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);
        cmdBindCompute(cmd, p.clear[flight & 1u].handle);
        vkCmdDispatch(cmd, 1, 1, 1);
        bufferBarrierComputeWriteToReadWrite(cmd, indirect.buffer, indirect.size);
    } else {
        bufferBarrierToCompute(cmd, indirect.buffer, 0, indirect.size, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        bufferBarrierToCompute(cmd, visible.buffer, 0, visible.size, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        bufferBarrierComputeWriteToReadWrite(cmd, flags.buffer, flags.size);
    }
    cmdBindCompute(cmd, late ? p.late[flight & 1u].handle : p.compact[flight & 1u].handle);
    dispatch1D(cmd, count);
    bufferBarrierComputeToIndirect(cmd, indirect.buffer, indirect.size);
    bufferBarrierComputeToMeshShader(cmd, visible.buffer, visible.size);
}

bool clusterPassCreate(ClusterPass& p, Device& d, const DescriptorHeaps& heaps) {
    clusterPassDestroy(p, d);
    PassBindings b{heaps};
    b.ubo(0, HeapBuf::Frame);
    b.sun();
    b.storageBuf(25, HeapBuf::Lights);
    b.storageBuf(26, HeapBuf::Clusters, false);
    b.storageBuf(27, HeapBuf::ClusterIndex, false);
    b.storageBuf(18, HeapBuf::Decals);
    const ShaderCreateDesc desc{
        .path = BH_SHADER_DIR "/cluster.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = b.raw.count,
        .mappings = b.raw.mappings,
    };
    if (!shaderCreate(d, desc, p.cs)) {
        spdlog::error("cluster light shader failed");
        clusterPassDestroy(p, d);
        return false;
    }
    return true;
}

void clusterPassDestroy(ClusterPass& p, Device& d) {
    shaderDestroy(d, p.cs);
}

void clusterPassRecord(VkCommandBuffer cmd, const ClusterPass& p) {
    cmdBindCompute(cmd, p.cs.handle);
    dispatch1D(cmd, 16u * 9u * 24u);
}

bool shadowCullCreate(ShadowCullPass& p, Device& d, const DescriptorHeaps& heaps) {
    shadowCullDestroy(p, d);
    PassBindings b{heaps};
    b.ubo(0, HeapBuf::Frame);
    b.sun();
    b.storageBuf(10, HeapBuf::Instances);
    b.storageBuf(18, HeapBuf::ShadowAll, false);
    b.storageBuf(32, HeapBuf::ShadowIndirect, false);
    PassBindings clearBind{heaps};
    clearBind.sun();
    clearBind.storageBuf(32, HeapBuf::ShadowIndirect, false);
    const ShaderCreateDesc clear{
        .path = BH_SHADER_DIR "/shadowcull.clear.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = clearBind.raw.count,
        .mappings = clearBind.raw.mappings,
    };
    const ShaderCreateDesc compact{
        .path = BH_SHADER_DIR "/shadowcull.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = 5,
        .mappings = b.raw.mappings,
    };
    if (!shaderCreate(d, clear, p.clear) || !shaderCreate(d, compact, p.compact)) {
        spdlog::error("shadow cull shader failed");
        shadowCullDestroy(p, d);
        return false;
    }
    return true;
}

void shadowCullDestroy(ShadowCullPass& p, Device& d) {
    shaderDestroy(d, p.clear);
    shaderDestroy(d, p.compact);
}

void shadowCullRecord(VkCommandBuffer cmd, const ShadowCullPass& p, const GpuBuffer& indirect, const GpuBuffer& lists, uint32_t count) {
    bufferBarrierToCompute(cmd, indirect.buffer, 0, indirect.size, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);
    bufferBarrierToCompute(cmd, lists.buffer, 0, lists.size, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    cmdBindCompute(cmd, p.clear.handle);
    vkCmdDispatch(cmd, 1, 1, 1);
    bufferBarrierComputeWriteToReadWrite(cmd, indirect.buffer, indirect.size);
    cmdBindCompute(cmd, p.compact.handle);
    dispatch1D(cmd, count);
    bufferBarrierComputeToIndirect(cmd, indirect.buffer, indirect.size);
    bufferBarrierComputeToMeshShader(cmd, lists.buffer, lists.size);
}

} // namespace burnhope
