#include "app/Engine.hpp"
#include "rhi/Barrier.hpp"

#include <spdlog/spdlog.h>
#include <SDL3/SDL.h>

#include <string_view>

namespace burnhope {
namespace {

bool writeHeapBuffersAndSamplers(Engine& e) {
    if (!heapWriteBuffer(e.heaps, e.device, HeapBuf::Frame, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            e.scene.frame.address, e.scene.frame.size)
        || !heapWriteBuffer(e.heaps, e.device, HeapBuf::Verts, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            e.scene.verts.address, e.scene.verts.size)
        || !heapWriteBuffer(e.heaps, e.device, HeapBuf::Indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            e.scene.indices.address, e.scene.indices.size)) {
        return false;
    }

    VkSamplerCreateInfo nearest{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 1.0f,
    };
    VkSamplerCreateInfo linear = nearest;
    linear.magFilter = VK_FILTER_LINEAR;
    linear.minFilter = VK_FILTER_LINEAR;
    linear.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    return heapWriteSampler(e.heaps, e.device, HeapSamp::Nearest, nearest)
        && heapWriteSampler(e.heaps, e.device, HeapSamp::Linear, linear);
}

bool writeHeapImages(Engine& e) {
    return heapWriteImage(e.heaps, e.device, HeapImg::Vis, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               e.vis.targets.vis, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(e.heaps, e.device, HeapImg::Depth, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               e.vis.targets.depth, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT)
        && heapWriteImage(e.heaps, e.device, HeapImg::HdrAStorage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
               e.shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(e.heaps, e.device, HeapImg::HdrASampled, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               e.shade.hdrA, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(e.heaps, e.device, HeapImg::HdrBStorage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
               e.shade.hdrB, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(e.heaps, e.device, HeapImg::HdrBSampled, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               e.shade.hdrB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
}

void recordPostVisBarriers(VkCommandBuffer cmd, const Engine& e) {
    rhiImageBarrier(cmd, e.vis.targets.vis.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    rhiImageBarrier(cmd, e.vis.targets.depth.image,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    rhiImageBarrier(cmd, e.shade.hdrA.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void recordPostShadeBarriers(VkCommandBuffer cmd, const Engine& e) {
    rhiImageBarrier(cmd, e.shade.hdrA.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    rhiImageBarrier(cmd, e.shade.hdrB.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void recordPostSsrBarriers(VkCommandBuffer cmd, const Engine& e) {
    rhiImageBarrier(cmd, e.shade.hdrB.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace

bool engineInit(Engine& e, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string_view(argv[i]) == "--use-classic-indirect") {
            e.classicIndirect = true;
        }
    }

    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — visbuffer → compute PBR + SSR (M2, descriptor_heap + BDA).");
    if (e.classicIndirect) {
        spdlog::info("--use-classic-indirect (no DGC yet; flag reserved)");
    }

    if (!windowCreate(e.window, 1600, 900, "Burnhope")) {
        spdlog::error("windowCreate failed: {}", SDL_GetError());
        return false;
    }
    if (!deviceCreate(e.device, e.window)) {
        return false;
    }
    if (!e.device.caps.meshShader) {
        spdlog::error("visbuffer meshlets need caps.meshShader");
        return false;
    }
    if (!swapchainCreate(e.swap, e.device, e.window.pixelW, e.window.pixelH)) {
        return false;
    }
    if (!descriptorHeapsCreate(e.heaps, e.device)) {
        return false;
    }
    if (!meshletGpuCreate(e.scene, e.device)) {
        return false;
    }
    meshletGpuWriteFrame(e.scene, e.swap.extent);
    if (!writeHeapBuffersAndSamplers(e)) {
        spdlog::error("heap buffer/sampler write failed");
        return false;
    }
    if (!visPassCreate(e.vis, e.device, e.heaps, e.swap.extent)
        || !shadePassCreate(e.shade, e.device, e.heaps, e.swap.extent)
        || !ssrPassCreate(e.ssr, e.device, e.heaps)
        || !tonemapPassCreate(e.tonemap, e.device, e.heaps)) {
        return false;
    }
    if (!writeHeapImages(e)) {
        spdlog::error("heap image write failed");
        return false;
    }
    if (!frameArenaCreate(e.arena, 16 * 1024 * 1024)) {
        spdlog::error("FrameArena 16MB failed");
        return false;
    }
    return true;
}

void engineShutdown(Engine& e) {
    if (e.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(e.device.device);
    }
    frameArenaDestroy(e.arena);
    tonemapPassDestroy(e.tonemap, e.device);
    ssrPassDestroy(e.ssr, e.device);
    shadePassDestroy(e.shade, e.device);
    visPassDestroy(e.vis, e.device);
    meshletGpuDestroy(e.scene, e.device);
    descriptorHeapsDestroy(e.heaps, e.device);
    swapchainDestroy(e.swap, e.device);
    deviceDestroy(e.device);
    windowDestroy(e.window);
}

bool engineTick(Engine& e) {
    windowPump(e.window);
    frameArenaReset(e.arena);

    auto recreate = [&]() -> bool {
        windowRefreshSize(e.window);
        if (e.window.pixelW == 0 || e.window.pixelH == 0) {
            return true;
        }
        vkDeviceWaitIdle(e.device.device);
        if (!swapchainRecreate(e.swap, e.device, e.window.pixelW, e.window.pixelH)) {
            return false;
        }
        if (!visPassResize(e.vis, e.device, e.swap.extent)
            || !shadePassResize(e.shade, e.device, e.swap.extent)) {
            return false;
        }
        meshletGpuWriteFrame(e.scene, e.swap.extent);
        return writeHeapImages(e);
    };

    if (e.window.resized) {
        e.window.resized = false;
        if (!recreate()) {
            spdlog::error("resize recreate failed");
            return false;
        }
        if (e.window.pixelW == 0 || e.window.pixelH == 0) {
            return true;
        }
    }

    FrameContext fc{};
    if (!swapchainBegin(e.swap, e.device, fc)) {
        return recreate();
    }

    heapBind(fc.cmd, e.heaps);
    visPassRecord(fc.cmd, e.swap.extent, e.vis);
    recordPostVisBarriers(fc.cmd, e);
    shadePassRecord(fc.cmd, e.swap.extent, e.shade);
    recordPostShadeBarriers(fc.cmd, e);
    ssrPassRecord(fc.cmd, e.swap.extent, e.ssr);
    recordPostSsrBarriers(fc.cmd, e);
    tonemapPassRecord(fc.cmd, e.swap, fc, e.tonemap);
    swapchainToPresent(e.swap, fc);
    if (!swapchainSubmitPresent(e.swap, e.device, fc)) {
        return recreate();
    }
    return true;
}

} // namespace burnhope
