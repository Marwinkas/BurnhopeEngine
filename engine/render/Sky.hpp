#pragma once

#include "gpu_scene/FrameUniforms.hpp"
#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "render/PassTargets.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;
struct FrameSun;

struct SkyPass {
    TrackedImage transmittance;
    TrackedImage multiScatter;
    TrackedImage skyView;
    TrackedImage cloud;
    GpuBuffer noise;
    bool noiseReady = false;
    ShaderExt transmittanceCs;
    ShaderExt multiCs;
    ShaderExt skyViewCs;
    ShaderExt cloudMarch;
    ShaderExt cloudUp;
    ShaderExt skyProbe;
};

void atmosphereApply(FrameSun& sun, const AtmosphereParams& atm);

[[nodiscard]] bool skyPassCreate(SkyPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void skyPassDestroy(SkyPass& p, Device& d);
void skyLutRecord(VkCommandBuffer cmd, SkyPass& p);
void skyProbeRecord(VkCommandBuffer cmd, SkyPass& p, VkBuffer probes, VkDeviceSize size);
void cloudRecord(VkCommandBuffer cmd, SkyPass& p, VkExtent2D extent);

} // namespace burnhope
