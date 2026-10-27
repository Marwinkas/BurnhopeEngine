#include "rhi/ui/UiPhoto.hpp"
#include "image/ImageFile.hpp"
#include "image/ImageUpload.hpp"
#include <spdlog/spdlog.h>

#include <mutex>

namespace burnhope {

bool uiPhotoUpload(
    UiRenderer& rhi,
    Device& device,
    DescriptorHeaps& heaps,
    uint8_t slot,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height) {
    if (slot >= kPhotoSlots || rgba == nullptr || width == 0 || height == 0) {
        return false;
    }
    {
        // The old image may still be sampled by an in-flight frame.
        std::lock_guard<std::mutex> queueLock(device.queueMutex);
        vkQueueWaitIdle(device.graphicsQueue);
    }
    gpuImageDestroy(rhi.photos[slot], device);
    if (!imageUploadUncompressed(rhi.photos[slot], device, rgba, width, height)) {
        return false;
    }
    const bool ok = heapWriteImage(
        heaps,
        device,
        kUiDescPhoto + slot,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        rhi.photos[slot],
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);
    if (!ok) {
        spdlog::error("uiPhotoUpload: heap write failed for slot {}", slot);
    }
    return ok;
}

bool uiPhotoLoad(
    UiRenderer& rhi,
    Device& device,
    DescriptorHeaps& heaps,
    PhotoMeta& meta,
    uint8_t slot,
    const char* path) {
    if (slot >= kPhotoSlots || path == nullptr || path[0] == '\0') {
        return false;
    }

    ImageCpu cpu{};
    if (!imageLoadFile(cpu, path)) {
        spdlog::warn("uiPhotoLoad: failed to load file '{}'", path);
        return false;
    }

    const uint32_t frameCount = cpu.frames == 0 ? 1u : cpu.frames;
    const uint32_t stackedHeight = cpu.height * frameCount;

    bool ok = cpu.hdr != 0
        ? gpuImageUploadHdr(rhi.photos[slot], device, cpu.rgb, cpu.width, cpu.height)
        : gpuImageUploadRgba(rhi.photos[slot], device, cpu.rgba, cpu.width, stackedHeight, frameCount);

    if (ok) {
        ok = heapWriteImage(
            heaps,
            device,
            kUiDescPhoto + slot,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            rhi.photos[slot],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    if (ok) {
        meta.frames = static_cast<uint8_t>(frameCount > kPhotoFrameCap ? kPhotoFrameCap : frameCount);
        for (uint32_t i = 0; i < kPhotoFrameCap; ++i) {
            meta.delayMs[i] = cpu.delayMs[i];
        }
        spdlog::info("uiPhotoLoad: slot {} [{}x{}] {} frames from '{}'", slot, cpu.width, cpu.height, meta.frames, path);
    } else {
        spdlog::error("uiPhotoLoad: GPU upload failed for slot {}", slot);
    }

    imageCpuFree(cpu);
    return ok;
}

} // namespace burnhope
