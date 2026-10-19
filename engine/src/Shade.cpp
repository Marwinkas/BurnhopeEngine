#include "render/Shade.hpp"

#include "render/RenderDefs.hpp"
#include "render/HeapBind.hpp"
#include "render/PassTargets.hpp"
#include "render/PassCommon.hpp"
#include "render/Sharc.hpp"
#include "rhi/ShaderObject.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace burnhope {

bool shadePassCreate(ShadePass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    shadePassDestroy(p, d);
    const bool aoOk = recreateTracked(
        p.ao, d, extent, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.contact, d, extent, VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflect, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflectHist[0], d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflectHist[1], d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
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
    p.reflectHistClear = true;
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
    sun.storageBuf(46, HeapBuf::CloudNoise);
    sun.knob(50, HeapBuf::Flags);
    sun.knob(55, HeapBuf::Shade);
    sun.knob(58, HeapBuf::Atm);
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
    punctual.knob(50, HeapBuf::Flags);
    punctual.knob(55, HeapBuf::Shade);
    PassBindings fog{heaps};
    fog.ubo(0, HeapBuf::Frame);
    fog.sun();
    fog.sampledImg(1, HeapImg::Vis);
    fog.sampledImg(2, HeapImg::Depth);
    fog.storageImg(3, HeapImg::HdrAStorage);
    fog.sampler(13, HeapSamp::Linear);
    fog.sampledImg(42, HeapImg::SkyViewSample);
    fog.knob(50, HeapBuf::Flags);
    fog.knob(54, HeapBuf::RcParams);
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
    contact.sampledImg(2, HeapImg::Depth);
    contact.sampler(13, HeapSamp::Nearest);
    contact.storageImg(46, HeapImg::Contact);
    contact.knob(50, HeapBuf::Flags);
    contact.knob(55, HeapBuf::Shade);
    PassBindings reflectTrace{heaps};
    reflectTrace.ubo(0, HeapBuf::Frame);
    reflectTrace.sampledImg(1, HeapImg::Vis);
    reflectTrace.sampledImg(2, HeapImg::Depth);
    reflectTrace.storageImg(3, HeapImg::HdrAStorage);
    reflectTrace.storageImg(4, HeapImg::Surf0);
    reflectTrace.storageImg(5, HeapImg::Surf1);
    reflectTrace.sampledImg(6, HeapImg::HiZMaxSample);
    reflectTrace.sampledImg(12, HeapImg::Textures);
    reflectTrace.storageBuf(14, HeapBuf::Materials);
    reflectTrace.sampler(23, HeapSamp::Linear);
    reflectTrace.sampler(24, HeapSamp::Wrap);
    reflectTrace.storageBuf(25, HeapBuf::Lights);
    reflectTrace.storageBuf(30, HeapBuf::Rc);
    reflectTrace.sampledImg(31, HeapImg::HiZMaxSample1);
    reflectTrace.storageImg(36, HeapImg::Reflect);
    reflectTrace.sampledImg(42, HeapImg::SkyViewSample);
    reflectTrace.sun();
    reflectTrace.knob(50, HeapBuf::Flags);
    reflectTrace.ubo(52, HeapBuf::Ssr);
    reflectTrace.storageBuf(70, kSharcHashSlot, false);
    reflectTrace.storageBuf(72, kSharcResolvedSlot, false);
    reflectTrace.ubo(73, static_cast<HeapBuf>(kSharcParamSlot));
    PassBindings reflectUp{heaps};
    reflectUp.ubo(0, HeapBuf::Frame);
    reflectUp.sampledImg(2, HeapImg::Depth);
    reflectUp.storageImg(3, HeapImg::HdrAStorage);
    reflectUp.storageImg(4, HeapImg::Surf0);
    reflectUp.storageImg(5, HeapImg::Surf1);
    reflectUp.storageImg(36, HeapImg::Reflect);
    reflectUp.storageImg(37, HeapImg::ReflectHist0);
    reflectUp.storageImg(38, HeapImg::ReflectHist1);
    reflectUp.ubo(52, HeapBuf::Ssr);
    reflectUp.raw.push(0, 60, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(*reflectUp.raw.heaps, kGradeSlot), static_cast<uint32_t>(reflectUp.raw.heaps->bufferDescSize));
    PassBindings relaxBind{heaps};
    relaxBind.ubo(0, HeapBuf::Frame);
    relaxBind.sampledImg(2, HeapImg::Depth);
    relaxBind.storageImg(3, HeapImg::HdrAStorage);
    relaxBind.storageImg(4, HeapImg::Surf0);
    relaxBind.storageImg(5, HeapImg::Surf1);
    relaxBind.storageImg(36, HeapImg::Reflect);
    relaxBind.storageImg(37, HeapImg::ReflectHist0);
    relaxBind.storageImg(38, HeapImg::ReflectHist1);
    relaxBind.raw.push(0, 60, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(*relaxBind.raw.heaps, kGradeSlot), static_cast<uint32_t>(relaxBind.raw.heaps->bufferDescSize));
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
        .pushBytes = sizeof(FlightPush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const ShaderCreateDesc reflectUpDesc{
        .path = BH_SHADER_DIR "/reflect.up.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = reflectUp.raw.count,
        .mappings = reflectUp.raw.mappings,
        .pushBytes = sizeof(FlightPush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const ShaderCreateDesc relaxDesc{
        .path = BH_SHADER_DIR "/relax.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = relaxBind.raw.count,
        .mappings = relaxBind.raw.mappings,
        .entry = "main",
        .pushBytes = sizeof(FlightPush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const ShaderCreateDesc fogDesc{
        .path = BH_SHADER_DIR "/shade.fog.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = fog.raw.count,
        .mappings = fog.raw.mappings,
    };
    if (!shaderCreate(d, skyDesc, p.sky) || !shaderCreate(d, sunDesc, p.sun) || !shaderCreate(d, punctualDesc, p.punctual)
        || !shaderCreate(d, fogDesc, p.fog) || !shaderCreate(d, gtaoDesc, p.gtao) || !shaderCreate(d, contactDesc, p.contactCs)
        || !shaderCreate(d, reflectTraceDesc, p.reflectTrace) || !shaderCreate(d, reflectUpDesc, p.reflectUp)
        || !shaderCreate(d, relaxDesc, p.relax)) {
        spdlog::error("shade compute shader failed");
        shadePassDestroy(p, d);
        return false;
    }
    return true;
}

void shadePassDestroy(ShadePass& p, Device& d) {
    shaderDestroy(d, p.relax);
    shaderDestroy(d, p.reflectUp);
    shaderDestroy(d, p.reflectTrace);
    shaderDestroy(d, p.contactCs);
    shaderDestroy(d, p.gtao);
    shaderDestroy(d, p.fog);
    shaderDestroy(d, p.punctual);
    shaderDestroy(d, p.sun);
    shaderDestroy(d, p.sky);
    destroyTracked(d, {&p.surf1, &p.surf0, &p.reflectHist[1], &p.reflectHist[0], &p.reflect, &p.contact, &p.ao, &p.hdrB, &p.hdrA});
}

bool shadePassResize(ShadePass& p, Device& d, VkExtent2D extent) {
    const bool aoOk = recreateTracked(
        p.ao, d, extent, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.contact, d, extent, VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflect, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflectHist[0], d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(
            p.reflectHist[1], d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    p.reflectHistClear = true;
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

void reflectRecord(VkCommandBuffer cmd, VkExtent2D extent, ShadePass& p, uint32_t flight) {
    const VkAccessFlags2 use = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if (p.reflectHistClear) {
        imageBarrier(cmd, p.reflectHist[0], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        imageBarrier(cmd, p.reflectHist[1], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        cmdBindCompute(cmd, p.reflectUp.handle);
        cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight, 2u);
        dispatch2D(cmd, extent, 8u);
        p.reflectHistClear = false;
        imageBarrier(cmd, p.reflectHist[0], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
        imageBarrier(cmd, p.reflectHist[1], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    }
    imageBarrier(cmd, p.reflect, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    cmdBindCompute(cmd, p.reflectTrace.handle);
    cmdPushFlight(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight);
    dispatch2D(cmd, extent, 8u);
    imageBarrier(cmd, p.reflect, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(cmd, p.reflectHist[0], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(cmd, p.reflectHist[1], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    cmdBindCompute(cmd, p.relax.handle);
    cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight, 0u);
    dispatch2D(cmd, extent, 8u);
    imageBarrier(cmd, p.reflectHist[flight & 1u], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight, 1u);
    dispatch2D(cmd, extent, 8u);
    imageBarrier(cmd, p.reflect, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight, 2u);
    dispatch2D(cmd, extent, 8u);
    imageBarrier(cmd, p.reflectHist[flight & 1u], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, use, VK_IMAGE_ASPECT_COLOR_BIT, true);
    cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight, 3u);
    dispatch2D(cmd, extent, 8u);
}

void shadeFogRecord(VkCommandBuffer cmd, VkExtent2D extent, const ShadePass& p) {
    dispatchCompute2D(cmd, p.fog, extent, 16u);
}

} // namespace burnhope
