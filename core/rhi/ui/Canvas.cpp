#include "rhi/ui/Canvas.hpp"

#include "platform/PlatformUtil.hpp"
#include "rhi/ui/Font.hpp"

#include <spdlog/spdlog.h>

#include <span>

namespace burnhope {

bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps) {
    canvasDestroy(c, d);
    c.device = &d;
    c.heaps = &heaps;
    if (!uiRendererCreate(c.rhi, d, heaps)) {
        canvasDestroy(c, d);
        return false;
    }
    for (uint8_t slot = 0; slot < kPhotoSlots; ++slot) {
        c.photoMeta[slot].frames = 1;
        c.photoMeta[slot].delayMs[0] = 100;
    }
    if (!canvasUseFont(c, 18.0f)) {
        spdlog::error("default sdf font failed");
        canvasDestroy(c, d);
        return false;
    }
    return true;
}

void canvasDestroy(Canvas& c, Device& d) {
    uiRendererDestroy(c.rhi, d);
    c.font = {};
    c.feedback = {};
    c.device = nullptr;
    c.heaps = nullptr;
}

bool canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, uint32_t flight, bool upload) {
    // A skipped frame (no prim buffer, zero extent) clears the one-shot feedback (ops, cursor) but keeps the text-focus
    // request: reporting "no text focus" would make the host stop SDL text input and lose the next keystrokes.
    const bool wantsText = c.feedback.wantsText;
    c.feedback = {};
    c.feedback.wantsText = wantsText;
    if (c.layer == nullptr || w < 1.0f || h < 1.0f) {
        return c.layer == nullptr;
    }
    const float dpi = c.dpi < 0.5f ? 1.0f : c.dpi;
    const float pointsW = w / dpi;
    const float pointsH = h / dpi;
    InputFrame local = input;
    local.pointer.x /= dpi;
    local.pointer.y /= dpi;
    local.pointer.dx /= dpi;
    local.pointer.dy /= dpi;

    uint32_t primCap = 0;
    UiPrimitive* dst = upload ? uiRendererPrimDst(c.rhi, flight, primCap) : nullptr;
    if (dst == nullptr || primCap == 0) {
        return false;
    }
    c.feedback = {};
    const uint32_t n = c.layer(c.layerUser, local, pointsW, pointsH, dst, primCap, c.feedback);
    uiRendererUploadPrims(c.rhi, d, flight, std::span<const UiPrimitive>(dst, n < primCap ? n : primCap), pointsW, pointsH);
    return true;
}

void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc, bool keepColor) {
    uiRendererRecord(cmd, c.rhi, sc, fc, c.rhi.primCount, keepColor, c.clear);
}

void canvasSetDpi(Canvas& c, float dpi) {
    c.dpi = dpi < 0.5f ? 1.0f : dpi;
}

void canvasApplyDpi(Canvas& c, float dpi, float previous) {
    if (previous < 0.1f || dpi < 0.1f) {
        return;
    }
    (void)canvasUseFont(c, c.font.fontPx * (dpi / previous));
}

bool canvasPointerOver(const Canvas& c) {
    return c.feedback.pointerOver;
}

bool canvasWantsRelative(const Canvas& c) {
    return c.feedback.wantsRelative;
}

bool canvasLoadPhoto(Canvas& c, uint8_t slot, const char* path) {
    if (slot >= kPhotoSlots || c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    return uiPhotoLoad(c.rhi, *c.device, *c.heaps, c.photoMeta[slot], slot, path);
}

bool canvasUploadPhoto(Canvas& c, uint8_t slot, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (slot >= kPhotoSlots || c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    return uiPhotoUpload(c.rhi, *c.device, *c.heaps, slot, rgba, width, height);
}

const PhotoMeta* canvasPhotoMeta(const Canvas& c, uint8_t slot) {
    return slot < kPhotoSlots ? &c.photoMeta[slot] : nullptr;
}

bool canvasSetFont(Canvas& c, const char* path, float pixelSize) {
    if (c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    if (!fontLoadSdf(c.font, *c.device, *c.heaps, c.rhi.font, path, pixelSize)) {
        return false;
    }
    uiRendererUploadFontTable(c.rhi, *c.device, c.font);
    return true;
}

bool canvasHasFont(const Canvas& c) {
    return c.font.ready;
}

bool canvasUseFont(Canvas& c, float pixelSize) {
    if (c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    if (!fontLoadDefaultSdf(c.font, *c.device, *c.heaps, c.rhi.font, pixelSize)) {
        char face[640]{};
        return platformResolveAsset("assets/fonts/Inter-Regular.ttf", face, sizeof(face)) && canvasSetFont(c, face, pixelSize);
    }
    uiRendererUploadFontTable(c.rhi, *c.device, c.font);
    return true;
}

} // namespace burnhope
