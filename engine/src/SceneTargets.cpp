#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/SceneFrameImpl.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace burnhope {

namespace {

bool writeKnob(DescriptorHeaps& heaps, Device& device, const GpuBuffer& post) {
    const uint32_t slot[] = {
        static_cast<uint32_t>(HeapBuf::Flags),
        static_cast<uint32_t>(HeapBuf::Gtao),
        static_cast<uint32_t>(HeapBuf::Ssr),
        static_cast<uint32_t>(HeapBuf::Ssrc),
        static_cast<uint32_t>(HeapBuf::RcParams),
        static_cast<uint32_t>(HeapBuf::Shade),
        static_cast<uint32_t>(HeapBuf::Sliders),
        static_cast<uint32_t>(HeapBuf::Bloom),
    };
    for (uint32_t i = 0; i < 8u; ++i) {
        const VkDeviceAddress at = post.address + 64ull * i;
        if (!heapWriteBufferFlight(heaps, device, 0, slot[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, at, 64)
            || !heapWriteBufferFlight(heaps, device, 1, slot[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, at + sizeof(FramePost), 64)) {
            return false;
        }
    }
    const VkDeviceAddress gradeAt = post.address + sizeof(FramePost) * 2;
    if (!heapWriteBufferFlight(heaps, device, 0, kGradeSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, gradeAt, sizeof(GradeBlock))
        || !heapWriteBufferFlight(heaps, device, 1, kGradeSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, gradeAt + sizeof(GradeBlock), sizeof(GradeBlock))) {
        return false;
    }
    return true;
}

} // namespace

bool heapImage(
    DescriptorHeaps& heaps,
    Device& device,
    SceneFrameImpl& s,
    uint32_t slot,
    VkDescriptorType type,
    const GpuImage& img,
    VkImageLayout layout,
    VkImageAspectFlags aspect) {
    const GpuImage* use = &img;
    if (use->image == VK_NULL_HANDLE || use->view == VK_NULL_HANDLE) {
        use = s.textures.empty() ? &img : &s.textures.front();
    }
    if (use->image == VK_NULL_HANDLE || use->view == VK_NULL_HANDLE || !imageSlotLive(slot)) {
        return false;
    }
    const uint32_t mips = type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? use->mips : 1u;
    return heapWriteImage(heaps, device, slot, type, *use, layout, aspect, mips);
}

bool bindHeap(SceneFrameImpl& s, Device& device, DescriptorHeaps& heaps) {
    const bool buffers = writeKnob(heaps, device, s.postBuf);
    struct StorageBind {
        HeapBuf slot;
        GpuBuffer* buf;
    };
    const StorageBind table[] = {
        {HeapBuf::Verts, &s.verts},
        {HeapBuf::Indices, &s.indices},
        {HeapBuf::Meshlets, &s.meshlets},
        {HeapBuf::Instances, &s.instances},
        {HeapBuf::Visible, &s.visible},
        {HeapBuf::Materials, &s.materialsBuf},
    };
    bool stored = buffers
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Frame), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.frameBuf.address, sizeof(FrameView))
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Frame), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.frameBuf.address + sizeof(FrameView), sizeof(FrameView))
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Sun), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.sunBuf.address, sizeof(FrameSun))
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Sun), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.sunBuf.address + sizeof(FrameSun), sizeof(FrameSun))
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Atm), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.atmBuf.address, sizeof(AtmosphereParams))
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Atm), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.atmBuf.address + sizeof(AtmosphereParams), sizeof(AtmosphereParams));
    for (const StorageBind& row : table) {
        stored = stored && heapWriteStorage(heaps, device, row.slot, *row.buf);
    }
    const VkDeviceSize list = alignUp(s.visibleCap) * sizeof(uint32_t);
    bool shadowLists = s.shadowVisible.address != 0;
    for (uint32_t flight = 0; shadowLists && flight < 2u; ++flight) {
        const VkDeviceAddress base = s.shadowVisible.address + list * 3u * flight;
        shadowLists = heapWriteBufferFlight(
            heaps, device, flight, static_cast<uint32_t>(HeapBuf::ShadowAll),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, base, list * 3u);
        for (uint32_t c = 0; shadowLists && c < 3u; ++c) {
            shadowLists = heapWriteBufferFlight(
                heaps, device, flight, shadowListSlot(c),
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, base + list * c, list);
        }
    }
    bool extra = stored && shadowLists;
    if (extra && s.pointVisible.address != 0) {
        extra = heapWriteStorage(heaps, device, static_cast<uint32_t>(HeapBuf::PointVisible), s.pointVisible.address, list * 3u);
    }
    for (uint32_t i = 0; extra && i < 6; ++i) {
        extra = heapWriteStorage(heaps, device, cubeListSlot(i), s.cubeVisible.address + list * i, list);
    }
    extra = extra && heapWriteStorage(heaps, device, static_cast<uint32_t>(HeapBuf::CubeVisible), s.cubeVisible.address, list * 6);
    const bool images =
        heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Vis), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.vis.targets.vis.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Depth), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.vis.targets.depth.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrAStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.hdrA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrBSampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.hdrA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImageFlight(heaps, device, 0, static_cast<uint32_t>(HeapImg::Shadow), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shadowDepth[0].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1)
        && heapWriteImageFlight(heaps, device, 1, static_cast<uint32_t>(HeapImg::Shadow), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shadowDepth[1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ShadowB), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shadowDepth[1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    const bool cascades = images;
    VkSamplerCreateInfo sampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    const bool pictures =
        heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrASampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.hdrA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrBStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.hdrB.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::AoStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.ao.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Ao), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.ao.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Contact), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.contact.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ContactSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.contact.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Reflect), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.reflect.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ReflectHist0), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.reflectHist[0].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ReflectHist1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.reflectHist[1].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Surf0), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.surf0.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Surf1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.surf1.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyTrans), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.transmittance.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyTransSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.transmittance.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyMs), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.multiScatter.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyView), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.skyView.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyViewSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.skyView.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Cloud), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.cloud.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CloudSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.cloud.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CloudShadow), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.shadow.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CloudShadowSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.shadow.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::BloomStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.tone.bloom.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Bloom), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.tone.bloom.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::PointShadow), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.pointDepth.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Cube), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.cubeColor.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CubeFill), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.cubeColor.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMax), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.hizMax[0].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMaxSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.hizMax[0].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMax1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.hizMax[1].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMaxSample1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.hizMax[1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VkSamplerCreateInfo wrap = sampler;
    wrap.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    wrap.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    wrap.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    wrap.maxLod = 12.0f;
    if (device.caps.samplerAnisotropy) {
        wrap.anisotropyEnable = VK_TRUE;
        wrap.maxAnisotropy = 16.0f;
    }
    const bool lamps =
        s.lightBuf.address != 0
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Lights), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s.lightBuf.address, sizeof(LightGpuData) * kLightCap)
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Lights), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s.lightBuf.address + sizeof(LightGpuData) * kLightCap, sizeof(LightGpuData) * kLightCap)
        && heapWriteStorage(heaps, device, HeapBuf::Clusters, s.cellBuf)
        && heapWriteStorage(heaps, device, HeapBuf::ClusterIndex, s.lightIndex)
        && heapWriteStorage(heaps, device, HeapBuf::Decals, s.decalBuf)
        && heapWriteStorage(heaps, device, HeapBuf::Probes, s.probeBuf)
        && heapWriteStorage(heaps, device, HeapBuf::CloudNoise, s.sky.noise)
        && heapWriteStorage(heaps, device, HeapBuf::Sectors, s.sectorBuf)
        && heapWriteStorage(heaps, device, HeapBuf::ShadowPages, s.shadowPageBuf)
        && heapWriteStorage(heaps, device, HeapBuf::TexPages, s.texPageBuf)
        && heapWriteStorage(heaps, device, HeapBuf::TexFeedback, s.texFeedbackBuf)
        && heapWriteStorage(heaps, device, HeapBuf::Candidates, s.candidates)
        && heapWriteStorage(heaps, device, HeapBuf::Indirect, s.indirect)
        && s.shadowIndirect.address != 0
        && heapWriteStorage(heaps, device, HeapBuf::ShadowIndirect, s.shadowIndirect);
    VkSamplerCreateInfo nearest = sampler;
    nearest.magFilter = VK_FILTER_NEAREST;
    nearest.minFilter = VK_FILTER_NEAREST;
    nearest.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (!(extra && cascades && pictures && lamps
        && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Nearest), nearest)
        && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Linear), sampler)
        && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Wrap), wrap))) {
        return false;
    }
    bool volumes = s.voxel.atoms.address != 0 && s.voxel.clip.address != 0
        && heapWriteStorage(heaps, device, HeapBuf::Voxel, s.voxel.atoms)
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::VxClip), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.voxel.clip.address, 256)
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::VxClip), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.voxel.clip.address + 256, 256);
    for (uint32_t flight = 0; volumes && flight < 2u; ++flight) {
        for (uint32_t cascade = 0; volumes && cascade < 3u; ++cascade) {
            const GpuImage& pic = s.voxel.volume[flight][cascade].image;
            volumes = heapImage(heaps, device, s, voxelImageSlot(false, flight, cascade), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, pic, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
                && heapImage(heaps, device, s, voxelImageSlot(true, flight, cascade), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, pic, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }
    if (!volumes) {
        return false;
    }
    for (uint32_t i = 0; i < s.textureSlots; ++i) {
        const GpuImage& tex = s.textures[i].image != VK_NULL_HANDLE ? s.textures[i] : s.textures[0];
        if (!heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Textures) + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return false;
        }
    }
    return restirBind(s.restir, device, heaps);
}

void sceneFrameReleaseGpu(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    if (device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.device);
    }
    gpuProfilerDestroy(scene->gpuTime, device);
    visPassDestroy(scene->vis, device);
    shadePassDestroy(scene->shade, device);
    skyPassDestroy(scene->sky, device);
    radianceDestroy(scene->radiance, device);
    sharcDestroy(scene->sharc, device);
    restirDestroy(scene->restir, device);
    fsrDestroy(scene->fsr, device);
    gtaoDestroy(scene->gtao, device);
    voxelDestroy(scene->voxel, device);
    hizPassDestroy(scene->hiz, device);
    cubePassDestroy(scene->cube, device);
    tonemapPassDestroy(scene->tone, device);
    gizmoPassDestroy(scene->gizmo, device);
    particlePassDestroy(scene->particles, device);
    fogDestroy(scene->fog, device);
    rainDestroy(scene->rain, device);
    destroyBuffers(device, {
        &scene->visible, &scene->shadowVisible, &scene->pointVisible, &scene->cubeVisible,
        &scene->candidates, &scene->indirect, &scene->shadowIndirect,
    });
    occlPassDestroy(scene->occl, device);
    clusterPassDestroy(scene->clusters, device);
    shadowCullDestroy(scene->shadowCull, device);
    destroyBuffers(device, {
        &scene->lightBuf, &scene->cellBuf, &scene->lightIndex, &scene->decalBuf, &scene->probeBuf,
        &scene->sectorBuf, &scene->shadowPageBuf, &scene->texPageBuf, &scene->texFeedbackBuf,
        &scene->meshlets, &scene->instances, &scene->indices, &scene->verts,
        &scene->frameBuf, &scene->sunBuf, &scene->postBuf, &scene->atmBuf, &scene->materialsBuf,
    });
    destroyImages(device, {
        &scene->shadowColor[0], &scene->shadowColor[1],
    });
    destroyTracked(device, {
        &scene->cubeColor, &scene->cubeDepth,
        &scene->shadowDepth[0], &scene->shadowDepth[1],
        &scene->pointDepth,
        &scene->hizMax[0], &scene->hizMax[1],
    });
    scene->pointReady = false;
    scene->cubeReady = false;
    for (GpuImage& img : scene->textures) {
        gpuImageDestroy(img, device);
    }
    scene->textures.clear();
    scene->textureSlots = 0;
    descriptorHeapsDestroy(scene->gpuHeap, device);
    scene->ready = false;
}

void sceneResizeTargets(SceneFrame* scene, Device& device, const Swapchain& swap, const FrameContext& frame, VkExtent2D renderExtent) {
    if (scene->ready && (scene->extent.width != renderExtent.width || scene->extent.height != renderExtent.height)) {
        const uint32_t other = frame.flight ^ 1u;
        VkResult otherFence = VK_SUCCESS;
        if (swap.flightFence[other] != VK_NULL_HANDLE) {
            otherFence = vkWaitForFences(device.device, 1, &swap.flightFence[other], VK_TRUE, 500'000'000ull);
        }
        if (otherFence != VK_SUCCESS) {
            spdlog::error("FSR resize held flight {} fence {} want {}x{} have {}x{}",
                other, static_cast<int>(otherFence), renderExtent.width, renderExtent.height,
                scene->extent.width, scene->extent.height);
            if (otherFence == VK_ERROR_DEVICE_LOST) {
                deviceMarkLost(device, "FSR resize", static_cast<int32_t>(otherFence));
            }
        } else {
        spdlog::info("FSR resize quality {} display {}x{} render {}x{} was {}x{}",
            scene->fsr.quality, swap.extent.width, swap.extent.height,
            renderExtent.width, renderExtent.height, scene->extent.width, scene->extent.height);
        const VkExtent2D hizExtent{renderExtent.width, renderExtent.height * 2u};
        const VkExtent2D bloomExtent{
            std::max(swap.extent.width / 2u, 1u),
            std::max(swap.extent.height / 2u, 1u) + std::max(swap.extent.height / 4u, 1u)
                + std::max(swap.extent.height / 8u, 1u) + std::max(swap.extent.height / 16u, 1u)};
        const bool resized = visPassResize(scene->vis, device, renderExtent)
            && shadePassResize(scene->shade, device, renderExtent)
            && skyPassResize(scene->sky, device, renderExtent)
            && recreateTracked(scene->hizMax[0], device, hizExtent, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->hizMax[1], device, hizExtent, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.bloom, device, bloomExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.history[0], device, renderExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.history[1], device, renderExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && restirResize(scene->restir, device, renderExtent)
            && fsrResize(scene->fsr, device, scene->gpuHeap, renderExtent, swap.extent)
            && gtaoResize(scene->gtao, device, scene->gpuHeap, renderExtent)
            && sharcResize(scene->sharc, device, scene->gpuHeap, renderExtent)
            && bindHeap(*scene, device, scene->gpuHeap);
        if (resized) {
            scene->extent = renderExtent;
            scene->tone.taaReady = 0;
        } else {
            spdlog::error("FSR resize failed quality {} render {}x{}", scene->fsr.quality, renderExtent.width, renderExtent.height);
            scene->ready = false;
        }
        }
    }
}

bool sceneCreateGpu(SceneFrame* scene, Device& device, const Swapchain& swap) {
    sceneFrameReleaseGpu(scene, device);
    const uint32_t vertAlloc = scene->vertCap > scene->vertCount ? scene->vertCap : scene->vertCount;
    const uint32_t indexAlloc = scene->indexCap > scene->indexCount ? scene->indexCap : scene->indexCount;
    const uint32_t instAlloc = scene->instanceCap > scene->instanceCount ? scene->instanceCap : scene->instanceCount;
    const uint32_t matAlloc = scene->materialCap > scene->materialCount ? scene->materialCap : std::max(scene->materialCount, 1u);
    const VkDeviceSize vertBytes = sizeof(GpuVertex) * vertAlloc;
    const VkDeviceSize indexBytes = sizeof(uint32_t) * indexAlloc;
    const VkDeviceSize instanceBytes = sizeof(GpuInstance) * instAlloc;
    const VkDeviceSize meshletBytes = sizeof(GpuMeshlet) * instAlloc;
    const VkDeviceSize visibleBytes = sizeof(uint32_t) * alignUp(scene->visibleCap);
    const VkDeviceSize listBytes = alignUp(visibleBytes);
    const bool buffers =
        createDefaultGpuBuffer(scene->frameBuf, device, sizeof(FrameView) * 2)
        && createDefaultGpuBuffer(scene->sunBuf, device, sizeof(FrameSun) * 2)
        && createDefaultGpuBuffer(scene->postBuf, device, sizeof(FramePost) * 2 + sizeof(GradeBlock) * 2)
        && createDefaultGpuBuffer(scene->atmBuf, device, sizeof(AtmosphereParams) * 2)
        && createDefaultGpuBuffer(scene->verts, device, vertBytes)
        && createDefaultGpuBuffer(scene->indices, device, indexBytes)
        && createDefaultGpuBuffer(scene->instances, device, instanceBytes)
        && createDefaultGpuBuffer(scene->meshlets, device, meshletBytes)
        && createDefaultGpuBuffer(scene->visible, device, listBytes * 4)
        && createDefaultGpuBuffer(scene->candidates, device, listBytes * 4)
        && createDefaultGpuBuffer(scene->indirect, device, 512, true, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        && createDefaultGpuBuffer(scene->shadowIndirect, device, 128, true, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        && createDefaultGpuBuffer(scene->shadowVisible, device, listBytes * 6)
        && createDefaultGpuBuffer(scene->pointVisible, device, listBytes * 3)
        && createDefaultGpuBuffer(scene->cubeVisible, device, listBytes * 6)
        && createDefaultGpuBuffer(scene->lightBuf, device, sizeof(LightGpuData) * kLightCap * 2)
        && createDefaultGpuBuffer(scene->cellBuf, device, sizeof(uint32_t) * 4 * 16 * 9 * 24)
        && createDefaultGpuBuffer(scene->lightIndex, device, sizeof(uint32_t) * 16 * 9 * 24 * 72)
        && createDefaultGpuBuffer(scene->decalBuf, device, sizeof(DecalGpu) * kDecalCap)
        && createDefaultGpuBuffer(scene->probeBuf, device, sizeof(ProbeGpu) * kProbeCount)
        && createDefaultGpuBuffer(scene->sectorBuf, device, sizeof(uint32_t) * std::max(instAlloc, 1u))
        && createDefaultGpuBuffer(scene->shadowPageBuf, device, sizeof(uint32_t) * 1024)
        && createDefaultGpuBuffer(scene->texPageBuf, device, sizeof(uint32_t) * 64)
        && createDefaultGpuBuffer(scene->texFeedbackBuf, device, sizeof(uint32_t) * 64)
        && createDefaultGpuBuffer(scene->materialsBuf, device, sizeof(MaterialGpuData) * matAlloc);
    const VkExtent2D pointExtent{512, 1536};
    const VkExtent2D cubeExtent{3072, 512};
    const VkExtent2D hizExtent{swap.extent.width, swap.extent.height * 2};
    const VkExtent2D shadowExtent{4096, 12288};
    const bool shadowImages =
        gpuImageCreate(scene->shadowColor[0], device, shadowExtent, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && gpuImageCreate(scene->shadowColor[1], device, shadowExtent, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateImage(scene->shadowDepth[0].image, device, shadowExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
        && recreateImage(scene->shadowDepth[1].image, device, shadowExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
        && recreateImage(scene->pointDepth.image, device, pointExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
        && recreateTracked(scene->cubeColor, device, cubeExtent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(scene->cubeDepth, device, cubeExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
        && recreateImage(scene->hizMax[0].image, device, hizExtent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateImage(scene->hizMax[1].image, device, hizExtent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!descriptorHeapsCreate(scene->gpuHeap, device, HeapLayout{
            .buffers = 96,
            .images = static_cast<uint32_t>(HeapImg::Count),
            .samplers = 3,
        })) {
        return false;
    }
    const bool passes = buffers && shadowImages
        && visPassCreate(scene->vis, device, scene->gpuHeap, swap.extent)
        && shadePassCreate(scene->shade, device, scene->gpuHeap, swap.extent)
        && skyPassCreate(scene->sky, device, scene->gpuHeap, swap.extent)
        && radianceCreate(scene->radiance, device, scene->gpuHeap)
        && sharcCreate(scene->sharc, device, scene->gpuHeap, swap.extent)
        && restirCreate(scene->restir, device, scene->gpuHeap, swap.extent)
        && fsrCreate(scene->fsr, device, scene->gpuHeap, swap.extent)
        && gtaoCreate(scene->gtao, device, scene->gpuHeap, swap.extent)
        && voxelCreate(scene->voxel, device, scene->gpuHeap)
        && hizPassCreate(scene->hiz, device, scene->gpuHeap)
        && occlPassCreate(scene->occl, device, scene->gpuHeap)
        && clusterPassCreate(scene->clusters, device, scene->gpuHeap)
        && shadowCullCreate(scene->shadowCull, device, scene->gpuHeap)
        && cubePassCreate(scene->cube, device, scene->gpuHeap)
        && tonemapPassCreate(scene->tone, device, scene->gpuHeap);
    (void)gizmoPassCreate(scene->gizmo, device, scene->gpuHeap);
    (void)particlePassCreate(scene->particles, device, scene->gpuHeap);
    (void)fogCreate(scene->fog, device, scene->gpuHeap);
    (void)rainCreate(scene->rain, device, scene->gpuHeap);
    const bool targets = passes
        && recreateTracked(scene->tone.bloom, device, VkExtent2D{
            std::max(swap.extent.width / 2u, 1u),
            std::max(swap.extent.height / 2u, 1u) + std::max(swap.extent.height / 4u, 1u)
                + std::max(swap.extent.height / 8u, 1u) + std::max(swap.extent.height / 16u, 1u)},
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(scene->tone.history[0], device, swap.extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && recreateTracked(scene->tone.history[1], device, swap.extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!targets || !loadTextures(*scene, device) || !bindHeap(*scene, device, scene->gpuHeap)) {
        return false;
    }
    uploadStatic(*scene);
    if (scene->probeBuf.mapped != nullptr) {
        auto* probes = static_cast<ProbeGpu*>(scene->probeBuf.mapped);
        for (uint32_t i = 0; i < kProbeCount; ++i) {
            probeSetGray(probes[i], 0.16f);
        }
    }
    if (scene->sectorBuf.mapped != nullptr) {
        std::memset(scene->sectorBuf.mapped, 0, scene->sectorBuf.size);
    }
    if (scene->texPageBuf.mapped != nullptr) {
        auto* pages = static_cast<uint32_t*>(scene->texPageBuf.mapped);
        for (uint32_t i = 0; i < 64u; ++i) {
            pages[i] = 1u;
        }
    }
    if (scene->texFeedbackBuf.mapped != nullptr) {
        std::memset(scene->texFeedbackBuf.mapped, 0, scene->texFeedbackBuf.size);
    }
    if (scene->shadowPageBuf.mapped != nullptr) {
        std::memset(scene->shadowPageBuf.mapped, 0, scene->shadowPageBuf.size);
    }
    if (scene->decalBuf.mapped != nullptr) {
        std::memset(scene->decalBuf.mapped, 0, scene->decalBuf.size);
    }
    scene->extent = swap.extent;
    if (scene->candidates.mapped != nullptr) {
        std::memset(scene->candidates.mapped, 0, scene->candidates.size);
    }
    if (scene->indirect.mapped != nullptr) {
        std::memset(scene->indirect.mapped, 0, scene->indirect.size);
    }
    scene->ready = true;
    return true;
}

} // namespace burnhope
