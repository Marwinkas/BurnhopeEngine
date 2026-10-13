#include "core/ui/UiPhoto.hpp"
#include "core/ui/Primitive.hpp"
#include "core/image/ImageFile.hpp"
#include <spdlog/spdlog.h>

namespace burnhope {

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