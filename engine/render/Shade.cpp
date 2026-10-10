#include "render/Shade.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapBind.hpp"
#include "render/PassTargets.hpp"
#include "render/PassCommon.hpp"
#include "rhi/ShaderObject.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace burnhope {

bool shadePassCreate(ShadePass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    shadePassDestroy(p, d);
    const bool aoOk = recreateTracked(
        p.ao, d, extent, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.contact, d, extent, VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflect, d, halfExtent(extent), VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    const bool hdrOk = recreateTracked(
            p.hdrA, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.hdrB, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.surf0, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.surf1, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!aoOk || !hdrOk) {
        shadePassDestroy(p, d);
        return false;
    }
    PassBindings sky{heaps};
    sky.ubo(0, HeapBuf::Frame);
    sky.storageImg(3, HeapImg::HdrAStorage);
    sky.sampledImg(42, HeapImg::SkyViewSample);
    sky.sampler(23, HeapSamp::Linear);
    sky.knob(50, HeapBuf::Flags);
    sky.knob(58, HeapBuf::Atm);
    PassBindings sun{heaps};
    sun.ubo(0, HeapBuf::Frame);
    sun.sun();
    sun.sampledImg(1, HeapImg::Vis);
    sun.sampledImg(2, HeapImg::Depth);
    sun.storageImg(3, HeapImg::HdrAStorage);
    sun.storageImg(4, HeapImg::Surf0);
    sun.storageImg(5, HeapImg::Surf1);
    sun.storageBuf(7, HeapBuf::Verts);
    sun.storageBuf(8, HeapBuf::Indices);
    sun.storageBuf(9, HeapBuf::Meshlets);
    sun.storageBuf(10, HeapBuf::Instances);
    sun.sampledImg(12, HeapImg::Textures);
    sun.storageBuf(14, HeapBuf::Materials);
    sun.sampledImg(15, HeapImg::Shadow);
    sun.sampledImg(16, HeapImg::Cube);
    sun.storageBuf(18, HeapBuf::Decals);
    sun.storageBuf(19, HeapBuf::Probes);
    sun.sampler(24, HeapSamp::Wrap);
    sun.storageBuf(26, HeapBuf::Clusters);
    sun.storageBuf(27, HeapBuf::ClusterIndex);
    sun.storageBuf(28, HeapBuf::TexPages);
    sun.storageBuf(29, HeapBuf::TexFeedback, false);
    sun.sampledImg(32, HeapImg::Ao);
    sun.sampledImg(42, HeapImg::SkyViewSample);
    sun.sampler(23, HeapSamp::Linear);
    sun.sampledImg(47, HeapImg::ContactSample);
    sun.knob(50, HeapBuf::Flags);
    sun.knob(55, HeapBuf::Shade);
    PassBindings punctual{heaps};
    punctual.ubo(0, HeapBuf::Frame);
    punctual.sun();
    punctual.sampledImg(1, HeapImg::Vis);
    punctual.sampledImg(2, HeapImg::Depth);
    punctual.storageImg(3, HeapImg::HdrAStorage);
    punctual.storageImg(4, HeapImg::Surf0);
    punctual.storageImg(5, HeapImg::Surf1);
    punctual.sampler(13, HeapSamp::Linear);
    punctual.sampledImg(17, HeapImg::PointShadow);
    punctual.storageBuf(25, HeapBuf::Lights);
    punctual.storageBuf(26, HeapBuf::Clusters);
    punctual.storageBuf(27, HeapBuf::ClusterIndex);
    punctual.sampledImg(32, HeapImg::Ao);
    punctual.knob(50, HeapBuf::Flags);
    punctual.knob(55, HeapBuf::Shade);
    PassBindings fog{heaps};
    fog.ubo(0, HeapBuf::Frame);
    fog.sun();
    fog.sampledImg(1, HeapImg::Vis);
    fog.sampledImg(2, HeapImg::Depth);
    fog.storageImg(3, HeapImg::HdrAStorage);
    fog.sampledImg(15, HeapImg::Shadow);
    fog.knob(50, HeapBuf::Flags);
    fog.knob(55, HeapBuf::Shade);
    PassBindings gtao{heaps};
    gtao.ubo(0, HeapBuf::Frame);
    gtao.sampledImg(2, HeapImg::Depth);
    gtao.storageImg(32, HeapImg::AoStorage);
    gtao.knob(50, HeapBuf::Flags);
    gtao.knob(51, HeapBuf::Gtao);
    PassBindings contact{heaps};
    contact.ubo(0, HeapBuf::Frame);
    contact.sun();
    contact.sampledImg(1, HeapImg::Vis);
    contact.sampledImg(2, HeapImg::Depth);
    contact.storageBuf(7, HeapBuf::Verts);
    contact.storageBuf(8, HeapBuf::Indices);
    contact.storageBuf(9, HeapBuf::Meshlets);
    contact.storageBuf(10, HeapBuf::Instances);
    contact.storageImg(46, HeapImg::Contact);
    contact.knob(55, HeapBuf::Shade);
    PassBindings reflectTrace{heaps};
    reflectTrace.ubo(0, HeapBuf::Frame);
    reflectTrace.sun();
    reflectTrace.sampledImg(1, HeapImg::Vis);
    reflectTrace.sampledImg(2, HeapImg::Depth);
    reflectTrace.storageImg(3, HeapImg::HdrAStorage);
    reflectTrace.storageImg(4, HeapImg::Surf0);
    reflectTrace.sampledImg(16, HeapImg::Cube);
    reflectTrace.sampler(24, HeapSamp::Wrap);
    reflectTrace.storageBuf(30, HeapBuf::Rc, false);
    reflectTrace.storageImg(36, HeapImg::Reflect);
    reflectTrace.knob(50, HeapBuf::Flags);
    PassBindings reflectUp{heaps};
    reflectUp.ubo(0, HeapBuf::Frame);
    reflectUp.sampledImg(2, HeapImg::Depth);
    reflectUp.storageImg(3, HeapImg::HdrAStorage);
    reflectUp.storageImg(36, HeapImg::Reflect);
    reflectUp.knob(50, HeapBuf::Flags);
    const ShaderCreateDesc contactDesc{
        .path = BH_SHADER_DIR "/contact.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = contact.raw.count,
        .mappings = contact.raw.mappings,
    };
    const ShaderCreateDesc gtaoDesc{
        .path = BH_SHADER_DIR "/gtao.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = gtao.raw.count,
        .mappings = gtao.raw.mappings,
    };
    const ShaderCreateDesc skyDesc{
        .path = BH_SHADER_DIR "/shade.sky.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = sky.raw.count,
        .mappings = sky.raw.mappings,
    };
    const ShaderCreateDesc sunDesc{
        .path = BH_SHADER_DIR "/shade.sun.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = sun.raw.count,
        .mappings = sun.raw.mappings,
    };
    const ShaderCreateDesc punctualDesc{
        .path = BH_SHADER_DIR "/shade.punctual.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = punctual.raw.count,
        .mappings = punctual.raw.mappings,
    };
    const ShaderCreateDesc reflectTraceDesc{
        .path = BH_SHADER_DIR "/reflect.trace.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = reflectTrace.raw.count,
        .mappings = reflectTrace.raw.mappings,
    };
    const ShaderCreateDesc reflectUpDesc{
        .path = BH_SHADER_DIR "/reflect.up.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = reflectUp.raw.count,
        .mappings = reflectUp.raw.mappings,
    };
    const ShaderCreateDesc fogDesc{
        .path = BH_SHADER_DIR "/shade.fog.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = fog.raw.count,
        .mappings = fog.raw.mappings,
    };
    if (!shaderCreate(d, skyDesc, p.sky) || !shaderCreate(d, sunDesc, p.sun) || !shaderCreate(d, punctualDesc, p.punctual)
        || !shaderCreate(d, fogDesc, p.fog) || !shaderCreate(d, gtaoDesc, p.gtao) || !shaderCreate(d, contactDesc, p.contactCs)
        || !shaderCreate(d, reflectTraceDesc, p.reflectTrace) || !shaderCreate(d, reflectUpDesc, p.reflectUp)) {
        spdlog::error("shade compute shader failed");
        shadePassDestroy(p, d);
        return false;
    }
    return true;
}

void shadePassDestroy(ShadePass& p, Device& d) {
    shaderDestroy(d, p.reflectUp);
    shaderDestroy(d, p.reflectTrace);
    shaderDestroy(d, p.contactCs);
    shaderDestroy(d, p.gtao);
    shaderDestroy(d, p.fog);
    shaderDestroy(d, p.punctual);
    shaderDestroy(d, p.sun);
    shaderDestroy(d, p.sky);
    destroyTracked(d, {&p.surf1, &p.surf0, &p.reflect, &p.contact, &p.ao, &p.hdrB, &p.hdrA});
}

bool shadePassResize(ShadePass& p, Device& d, VkExtent2D extent) {
    const bool aoOk = recreateTracked(
        p.ao, d, extent, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.contact, d, extent, VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflect, d, halfExtent(extent), VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    return recreateTracked(
            p.hdrA, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.hdrB, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.surf0, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.surf1, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && aoOk;
}

void shadeAoRecord(VkCommandBuffer cmd, VkExtent2D extent, ShadePass& p) {
    imageBarrier(cmd, p.ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.gtao, halfExtent(extent));
    imageBarrier(cmd, p.ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void shadeSkyRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    dispatchCompute2D(cmd, p.sky, extent, 16u);
}

void shadeSunRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    dispatchCompute2D(cmd, p.sun, extent, 16u);
}

void shadePunctualRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    dispatchCompute2D(cmd, p.punctual, extent, 16u);
}

void contactRecord(VkCommandBuffer cmd, VkExtent2D extent, ShadePass& p) {
    imageBarrier(cmd, p.contact, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.contactCs, extent, 8u);
    imageBarrier(cmd, p.contact, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void reflectRecord(VkCommandBuffer cmd, VkExtent2D extent, ShadePass& p, uint32_t lightProfile) {
    if (lightProfile < 1u) {
        return;
    }
    const VkExtent2D traceExtent = lightProfile >= 2u ? extent : halfExtent(extent);
    imageBarrier(cmd, p.reflect, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.reflectTrace, traceExtent, 8u);
    if (lightProfile == 1u) {
        imageBarrier(cmd, p.reflect, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
        dispatchCompute2D(cmd, p.reflectUp, extent, 8u);
    }
}

void shadeFogRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    dispatchCompute2D(cmd, p.fog, extent, 16u);
}

} // namespace burnhope
