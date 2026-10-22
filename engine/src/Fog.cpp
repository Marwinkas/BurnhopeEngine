#include "render/Fog.hpp"

#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/RenderDefs.hpp"
#include "rhi/Barrier.hpp"

namespace burnhope {

bool fogCreate(FogVolume& fog, Device& device, DescriptorHeaps& heaps) {
    fogDestroy(fog, device);
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!gpuImageCreateFroxel(fog.scatter.image, device, kFogW, kFogH, kFogD, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)
        || !gpuImageCreateFroxel(fog.integrated.image, device, kFogW, kFogH, kFogD, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)) {
        fogDestroy(fog, device);
        return false;
    }
    PassBindings scatter{heaps};
    scatter.ubo(0, HeapBuf::Frame);
    scatter.sun();
    scatter.knob(54, HeapBuf::RcParams);
    scatter.knob(55, HeapBuf::Shade);
    scatter.storageImg(90, kFogScatterSlot);
    PassBindings integrate{heaps};
    integrate.storageImg(90, kFogScatterSlot);
    integrate.storageImg(91, kFogIntSlot);
    PassBindings apply{heaps};
    apply.ubo(0, HeapBuf::Frame);
    apply.sampledImg(2, HeapImg::Depth);
    apply.storageImg(3, HeapImg::HdrAStorage);
    apply.storageImg(91, kFogIntSlot);
    const ShaderCreateDesc scatterDesc{
        .path = BH_SHADER_DIR "/fog.scatter.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = scatter.raw.count,
        .mappings = scatter.raw.mappings,
    };
    const ShaderCreateDesc integrateDesc{
        .path = BH_SHADER_DIR "/fog.integrate.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = integrate.raw.count,
        .mappings = integrate.raw.mappings,
    };
    const ShaderCreateDesc applyDesc{
        .path = BH_SHADER_DIR "/fog.apply.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = apply.raw.count,
        .mappings = apply.raw.mappings,
    };
    if (!shaderCreate(device, scatterDesc, fog.scatterCs)
        || !shaderCreate(device, integrateDesc, fog.integrateCs)
        || !shaderCreate(device, applyDesc, fog.applyCs)) {
        fogDestroy(fog, device);
        return false;
    }
    const bool bound = heapWriteImage(heaps, device, kFogScatterSlot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, fog.scatter.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1)
        && heapWriteImage(heaps, device, kFogIntSlot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, fog.integrated.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    if (!bound) {
        fogDestroy(fog, device);
        return false;
    }
    fog.ready = true;
    return true;
}

void fogDestroy(FogVolume& fog, Device& device) {
    shaderDestroy(device, fog.scatterCs);
    shaderDestroy(device, fog.integrateCs);
    shaderDestroy(device, fog.applyCs);
    gpuImageDestroy(fog.scatter.image, device);
    gpuImageDestroy(fog.integrated.image, device);
    fog.scatter = {};
    fog.integrated = {};
    fog.ready = false;
}

void fogRecord(VkCommandBuffer cmd, FogVolume& fog, TrackedImage& hdr, VkExtent2D extent) {
    if (!fog.ready || hdr.image.image == VK_NULL_HANDLE) {
        return;
    }
    imageBarrier(cmd, fog.scatter, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    cmdBindCompute(cmd, fog.scatterCs.handle);
    vkCmdDispatch(cmd, (kFogW + 7u) / 8u, (kFogH + 7u) / 8u, (kFogD + 3u) / 4u);
    imageBarrier(cmd, fog.scatter, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(cmd, fog.integrated, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    cmdBindCompute(cmd, fog.integrateCs.handle);
    vkCmdDispatch(cmd, (kFogW + 7u) / 8u, (kFogH + 7u) / 8u, 1u);
    imageBarrier(cmd, fog.integrated, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(cmd, hdr, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    cmdBindCompute(cmd, fog.applyCs.handle);
    dispatch2D(cmd, extent, 8u);
}

} // namespace burnhope
