#include "render/Sky.hpp"

#include "render/RenderDefs.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "rhi/ShaderObject.hpp"

#include <cmath>
#include <spdlog/spdlog.h>

namespace burnhope {

void atmosphereApply(FrameSun& sun, const AtmosphereParams& atm) {
    const float elev = atm.sunZenith * 0.017453292f;
    const float az = atm.sunAzimuth * 0.017453292f;
    const float c = std::cos(elev);
    const float dir[3] = {c * std::cos(az), std::sin(elev), c * std::sin(az)};
    const float fade = std::min(std::max(dir[1] * 1.15f + 0.02f, 0.0f), 1.0f);
    const float warm = std::min(std::max(1.0f - dir[1] * 3.0f, 0.0f), 1.0f);
    sun.sunDirIntensity[0] = dir[0];
    sun.sunDirIntensity[1] = dir[1];
    sun.sunDirIntensity[2] = dir[2];
    sun.sunDirIntensity[3] = (atm.sunIntensity / 5000.0f) * std::max(fade, 0.02f);
    sun.sunColor[0] = 1.0f;
    sun.sunColor[1] = 0.97f + (0.35f - 0.97f) * warm * (1.0f - fade * 0.5f);
    sun.sunColor[2] = 0.92f + (0.12f - 0.92f) * warm * (1.0f - fade * 0.5f);
}

bool skyPassCreate(SkyPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    skyPassDestroy(p, d);
    const VkExtent2D trans{256u, 64u};
    const VkExtent2D ms{32u, 32u};
    const VkExtent2D view{192u, 108u};
    const VkExtent2D half = halfExtent(extent);
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const bool pics = recreateTracked(p.transmittance, d, trans, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(p.multiScatter, d, ms, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(p.skyView, d, view, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(p.cloud, d, half, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    constexpr uint32_t kNoiseFloats = 128u * 128u * 128u + 32u * 32u * 32u;
    const bool noiseOk = pics && createDefaultGpuBuffer(p.noise, d, sizeof(float) * kNoiseFloats);
    if (!noiseOk || p.noise.mapped == nullptr) {
        skyPassDestroy(p, d);
        return false;
    }
    p.noiseReady = false;
    PassBindings noiseBind{heaps};
    noiseBind.storageBuf(46, HeapBuf::CloudNoise, false);
    PassBindings transBind{heaps};
    transBind.knob(58, HeapBuf::Atm);
    transBind.storageImg(40, HeapImg::SkyTrans);
    PassBindings msBind{heaps};
    msBind.knob(58, HeapBuf::Atm);
    msBind.storageImg(41, HeapImg::SkyMs);
    msBind.sampledImg(43, HeapImg::SkyTransSample);
    msBind.sampler(23, HeapSamp::Linear);
    PassBindings viewBind{heaps};
    viewBind.knob(58, HeapBuf::Atm);
    viewBind.storageImg(42, HeapImg::SkyView);
    viewBind.storageImg(41, HeapImg::SkyMs);
    viewBind.sampledImg(43, HeapImg::SkyTransSample);
    viewBind.sampler(23, HeapSamp::Linear);
    PassBindings marchBind{heaps};
    marchBind.ubo(0, HeapBuf::Frame);
    marchBind.storageImg(44, HeapImg::Cloud);
    marchBind.storageBuf(46, HeapBuf::CloudNoise);
    marchBind.knob(58, HeapBuf::Atm);
    PassBindings upBind{heaps};
    upBind.ubo(0, HeapBuf::Frame);
    upBind.sampledImg(1, HeapImg::Vis);
    upBind.sampledImg(2, HeapImg::Depth);
    upBind.storageImg(3, HeapImg::HdrAStorage);
    upBind.sampledImg(45, HeapImg::CloudSample);
    upBind.sampler(23, HeapSamp::Linear);
    upBind.knob(50, HeapBuf::Flags);
    upBind.knob(58, HeapBuf::Atm);
    const ShaderCreateDesc transDesc{
        .path = BH_SHADER_DIR "/sky.trans.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = transBind.raw.count,
        .mappings = transBind.raw.mappings,
    };
    const ShaderCreateDesc msDesc{
        .path = BH_SHADER_DIR "/sky.ms.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = msBind.raw.count,
        .mappings = msBind.raw.mappings,
    };
    const ShaderCreateDesc viewDesc{
        .path = BH_SHADER_DIR "/sky.view.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = viewBind.raw.count,
        .mappings = viewBind.raw.mappings,
    };
    const ShaderCreateDesc marchDesc{
        .path = BH_SHADER_DIR "/cloud.march.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = marchBind.raw.count,
        .mappings = marchBind.raw.mappings,
    };
    PassBindings probeBind{heaps};
    probeBind.ubo(0, HeapBuf::Frame);
    probeBind.storageBuf(19, HeapBuf::Probes, false);
    probeBind.sampler(23, HeapSamp::Linear);
    probeBind.sampledImg(42, HeapImg::SkyViewSample);
    const ShaderCreateDesc probeDesc{
        .path = BH_SHADER_DIR "/sky.probe.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = probeBind.raw.count,
        .mappings = probeBind.raw.mappings,
    };
    const ShaderCreateDesc upDesc{
        .path = BH_SHADER_DIR "/cloud.up.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = upBind.raw.count,
        .mappings = upBind.raw.mappings,
    };
    const ShaderCreateDesc noiseDesc{
        .path = BH_SHADER_DIR "/cloud.noise.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = noiseBind.raw.count,
        .mappings = noiseBind.raw.mappings,
    };
    if (!shaderCreate(d, transDesc, p.transmittanceCs) || !shaderCreate(d, msDesc, p.multiCs) || !shaderCreate(d, viewDesc, p.skyViewCs)
        || !shaderCreate(d, marchDesc, p.cloudMarch) || !shaderCreate(d, upDesc, p.cloudUp)
        || !shaderCreate(d, noiseDesc, p.noiseCs)
        || !shaderCreate(d, probeDesc, p.skyProbe)) {
        spdlog::error("sky shader failed");
        skyPassDestroy(p, d);
        return false;
    }
    return true;
}

bool skyPassResize(SkyPass& p, Device& d, VkExtent2D extent) {
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return recreateTracked(p.cloud, d, halfExtent(extent), VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT);
}

void skyPassDestroy(SkyPass& p, Device& d) {
    shaderDestroy(d, p.skyProbe);
    shaderDestroy(d, p.noiseCs);
    shaderDestroy(d, p.cloudUp);
    shaderDestroy(d, p.cloudMarch);
    shaderDestroy(d, p.skyViewCs);
    shaderDestroy(d, p.multiCs);
    shaderDestroy(d, p.transmittanceCs);
    destroyTracked(d, {&p.cloud, &p.skyView, &p.multiScatter, &p.transmittance});
    destroyBuffers(d, {&p.noise});
    p.noiseReady = false;
}

void skyLutRecord(VkCommandBuffer cmd, SkyPass& p) {
    imageBarrier(cmd, p.transmittance, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.transmittanceCs, p.transmittance.image.extent, 8u);
    imageBarrier(cmd, p.transmittance, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(cmd, p.multiScatter, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.multiCs, p.multiScatter.image.extent, 8u);
    imageBarrier(cmd, p.multiScatter, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(cmd, p.skyView, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.skyViewCs, p.skyView.image.extent, 8u);
    imageBarrier(cmd, p.skyView, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
}

void skyProbeRecord(VkCommandBuffer cmd, SkyPass& p, VkBuffer probes, VkDeviceSize size) {
    cmdBindCompute(cmd, p.skyProbe.handle);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
    bufferBarrierComputeWriteToRead(cmd, probes, 0, size);
}

void cloudRecord(VkCommandBuffer cmd, SkyPass& p, VkExtent2D extent) {
    if (!p.noiseReady && p.noise.buffer != VK_NULL_HANDLE && p.noiseCs.handle != VK_NULL_HANDLE) {
        bufferBarrierHostToShader(cmd, p.noise.buffer, p.noise.size, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        cmdBindCompute(cmd, p.noiseCs.handle);
        vkCmdDispatch(cmd, 32, 32, 32);
        bufferBarrierComputeWriteToRead(cmd, p.noise.buffer, 0, p.noise.size);
        p.noiseReady = true;
    }
    imageBarrier(cmd, p.cloud, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.cloudMarch, halfExtent(extent), 8u);
    imageBarrier(cmd, p.cloud, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
    dispatchCompute2D(cmd, p.cloudUp, extent, 8u);
}

} // namespace burnhope
