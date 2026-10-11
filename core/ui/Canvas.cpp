#include "ui/Canvas.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "platform/Trace.hpp"

#include <cmath>
#include "ui/Font.hpp"
#include "ui/State.hpp"
#include "image/ImageFile.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>


namespace burnhope {
bool canvasLoadImage(Canvas& c, uint8_t slot, const char* path);

bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps) {
    canvasDestroy(c, d);
    c.state = new UiState();
    c.device = &d;
    c.heaps = &heaps;
    if (!uiRendererCreate(c.rhi, d, heaps)) {
        canvasDestroy(c, d);
        return false;
    }
    if (!canvasUseFont(c, 20.0f)) {
        spdlog::error("default sdf font failed");
        canvasDestroy(c, d);
        return false;
    }
    return true;
}

void canvasDestroy(Canvas& c, Device& d) {
    uiRendererDestroy(c.rhi, d);
    if (c.state != nullptr) {
        if (c.state->plug.shutdown != nullptr) {
            c.state->plug.shutdown(*c.state);
        }
        delete c.state;
        c.state = nullptr;
    }
    c.device = nullptr;
    c.heaps = nullptr;
}

void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a) {
    if (id >= kUiCap) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    UiAnim& anim = s.anim[id];
    anim.fr = paint != nullptr ? paint->r : r;
    anim.fg = paint != nullptr ? paint->g : g;
    anim.fb = paint != nullptr ? paint->b : b;
    anim.fa = paint != nullptr ? paint->a : a;
    if (anim.on != 0 && anim.dur > 0.0f) {
        float u = (s.time - anim.t0) / anim.dur;
        if (u < 0.0f) {
            u = 0.0f;
        }
        if (u > 1.0f) {
            u = 1.0f;
        }
        anim.fr += (anim.tr - anim.fr) * u;
        anim.fg += (anim.tg - anim.fg) * u;
        anim.fb += (anim.tb - anim.fb) * u;
        anim.fa += (anim.ta - anim.fa) * u;
    }
    anim.tr = r;
    anim.tg = g;
    anim.tb = b;
    anim.ta = a;
    anim.t0 = s.time;
    anim.dur = s.look.motion > 0.0f ? s.look.motion : 0.01f;
    anim.on = 1;
}

bool uiTickMotion(UiState& s) {
    bool live = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.anim[id].on != 0) {
            live = true;
            if (s.time - s.anim[id].t0 >= s.anim[id].dur) {
                s.anim[id].on = 0;
            }
        }
    }
    return live;
}

void canvasSetLook(Canvas& c, const UiLook& look) {
    if (c.state == nullptr) {
        return;
    }
    const float glyphH = c.state->look.glyphH;
    const float glyphW = c.state->look.glyphW;
    const float advance = c.state->look.advance;
    const float title = c.state->look.titleBar;
    const float border = c.state->look.resizeBorder;
    c.state->look = look;
    c.state->look.glyphH = glyphH;
    c.state->look.glyphW = glyphW;
    c.state->look.advance = advance;
    c.state->look.titleBar = title;
    c.state->look.resizeBorder = border;
    uiRestyle(*c.state);
}

UiLook canvasLook(const Canvas& c) {
    if (c.state == nullptr) {
        return {};
    }
    return c.state->look;
}

void canvasSetBuilder(Canvas& c, UiBuilder builder, void* user) {
    c.builder = builder;
    c.builderUser = user;
}

void photoPush(Canvas& c, uint8_t slot) {
    if (c.state == nullptr || slot >= kPhotoSlots) {
        return;
    }
    uiApplyPhotoMeta(*c.state, slot, c.rhi.photoMeta[slot].frames, c.rhi.photoMeta[slot].delayMs, kImageFrames);
}

void canvasReload(Canvas& c) {
    if (c.state == nullptr) {
        return;
    }
    uiClear(*c.state);
    if (c.builder != nullptr) {
        c.builder(c, c.builderUser);
    }
    photoPush(c, 0);
    c.state->layoutDirty = true;
    c.state->visualDirty = true;
    spdlog::info("ui: {} flecs nodes, yoga + 1 draw", c.state->count);
}

void canvasOnFocus(Canvas& c, UiFocusFn fn, void* user) {
    if (c.state != nullptr) {
        c.state->focusFn = fn;
        c.state->focusUser = user;
    }
}

void canvasSetClick(Canvas& c, UiClickFn fn, void* user) {
    if (c.state != nullptr) {
        c.state->onClick = fn;
        c.state->clickUser = user;
    }
}

bool canvasListen(Canvas& c, UiValueFn fn, void* user) {
    return c.state != nullptr && uiListen(*c.state, fn, user);
}

bool canvasLoadImage(Canvas& c, uint8_t slot, const char* path) {
    if (slot >= kPhotoSlots || c.device == nullptr || c.heaps == nullptr || path == nullptr) {
        return false;
    }
    ImageCpu cpu{};
    if (!imageLoadFile(cpu, path)) {
        return false;
    }
    const uint32_t stacked = cpu.height * (cpu.frames == 0 ? 1u : cpu.frames);
    bool ok = cpu.hdr != 0
        ? gpuImageUploadHdr(c.rhi.photos[slot], *c.device, cpu.rgb, cpu.width, cpu.height)
        : gpuImageUploadRgba(c.rhi.photos[slot], *c.device, cpu.rgba, cpu.width, stacked, cpu.frames == 0 ? 1u : cpu.frames);
    if (ok) {
        ok = heapWriteImage(
            *c.heaps,
            *c.device,
            kUiDescPhoto + slot,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            c.rhi.photos[slot],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    if (ok) {
        c.rhi.photoMeta[slot].frames = static_cast<uint8_t>(cpu.frames == 0 ? 1u : cpu.frames);
        for (uint32_t i = 0; i < kImageFrames; ++i) {
            c.rhi.photoMeta[slot].delayMs[i] = cpu.delayMs[i];
        }
        photoPush(c, slot);
        spdlog::info("image slot {} {}x{} x{} frames", slot, cpu.width, cpu.height, cpu.frames);
    }
    imageCpuFree(cpu);
    return ok;
}

int canvasCursor(const Canvas& c) {
    if (c.state == nullptr) {
        return 0;
    }
    if (c.state->cursorN > 0) {
        return c.state->cursorStack[c.state->cursorN - 1];
    }
    return c.state->cursor;
}

bool canvasSetFont(Canvas& c, const char* path, float pixelSize) {
    if (c.state == nullptr || c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    if (!fontLoadSdf(*c.state, *c.device, *c.heaps, c.rhi.font, path, pixelSize)) {
        return false;
    }
    uiRendererUploadFontTable(c.rhi, *c.device, c.state->font);
    c.state->look.glyphH = pixelSize;
    const int em = static_cast<int>('M' - 32);
    if (c.state->font.fontPx > 1.0f && c.state->font.glyphs[em].advance > 0.0f) {
        c.state->look.advance = c.state->font.glyphs[em].advance * (pixelSize / c.state->font.fontPx);
    }
    uiRestyle(*c.state);
    return true;
}

bool canvasUseFont(Canvas& c, float pixelSize) {
        const char* faces[] = {
        "assets/fonts/Inter-Regular.ttf",
        "../assets/fonts/Inter-Regular.ttf",
        "../../assets/fonts/Inter-Regular.ttf",
        "../../../assets/fonts/Inter-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    };
    for (const char* path : faces) {
        if (canvasSetFont(c, path, pixelSize)) {
            return true;
        }
    }
    spdlog::error("sdf font failed at {} px", pixelSize);
    return false;
}

bool canvasTakeClip(Canvas& c, char* dst, uint8_t cap) {
    if (c.state == nullptr || c.state->clipPut == 0 || dst == nullptr || cap == 0) {
        return false;
    }
    const uint8_t n = c.state->clipLen < static_cast<uint8_t>(cap - 1) ? c.state->clipLen : static_cast<uint8_t>(cap - 1);
    for (uint8_t i = 0; i < n; ++i) {
        dst[i] = c.state->clip[i];
    }
    dst[n] = '\0';
    c.state->clipPut = 0;
    return n > 0;
}

void canvasChrome(const Canvas& c, float& titleBar, float& resizeBorder) {
    titleBar = 40.0f;
    resizeBorder = 6.0f;
    if (c.state != nullptr) {
        titleBar = c.state->look.titleBar;
        resizeBorder = c.state->look.resizeBorder;
    }
}

UiEvent canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, float timeSec, uint32_t flight, bool upload) {
    if (c.state == nullptr || c.state->count == 0 || w < 1.0f || h < 1.0f) {
        return UiEvent::None;
    }
    UiState& s = *c.state;
    s.time = timeSec;
    const float dpi = s.dpi < 0.5f ? 1.0f : s.dpi;
    const float pointsW = w / dpi;
    const float pointsH = h / dpi;
    InputFrame local = input;
    local.pointer.x /= dpi;
    local.pointer.y /= dpi;
    local.pointer.dx /= dpi;
    local.pointer.dy /= dpi;
    uiLayout(s, pointsW, pointsH);
    const UiEvent ev = uiApplyInput(s, local);
    if (s.layoutDirty) {
        uiLayout(s, pointsW, pointsH);
    }
    if (uiTickMotion(s)) {
        s.visualDirty = true;
    }
    uint32_t primCap = 0;
    UiPrimitive* dst = upload ? uiRendererPrimDst(c.rhi, flight, primCap) : nullptr;
    if (s.visualDirty && dst != nullptr && primCap > 0) {
        const uint32_t n = uiEmit(s, dst, primCap);
        uiRendererUploadPrims(c.rhi, d, flight, std::span<const UiPrimitive>(dst, n), pointsW, pointsH);
        s.visualDirty = false;
    }
    if (s.dumpLayout != 0) {
        uiDumpLayout(s);
        spdlog::info("ui draw {}", c.rhi.primCount);
        s.dumpLayout = 0;
    }
    return ev;
}

void canvasTakeWindowOps(Canvas& c, WindowOps& out) {
    out = {};
    if (c.state == nullptr) {
        return;
    }
    UiState& s = *c.state;
    out.close = s.opClose;
    out.minimize = s.opMinimize;
    out.toggleMaximize = s.opMaximize;
    out.alwaysOnTop = s.opTop;
    out.opacity = s.opOpacity;
    s.opClose = false;
    s.opMinimize = false;
    s.opMaximize = false;
    s.opTop = -1;
    s.opOpacity = -1.0f;
}

bool canvasAddCommand(Canvas& c, const char* name, UiCommandFn fn, void* user) {
    if (c.state == nullptr) {
        return false;
    }
    return uiCommandAdd(*c.state, name, fn, user);
}

bool canvasWantsText(const Canvas& c) {
    if (c.state == nullptr) {
        return false;
    }
    if (c.state->plug.wantsText != nullptr && c.state->plug.wantsText(*c.state)) {
        return true;
    }
    if (c.state->focused == kUiNone) {
        return false;
    }
    return c.state->ent[c.state->focused].try_get<UiField>() != nullptr;
}

bool canvasWantsRelative(const Canvas& c) {
    return c.state != nullptr && c.state->relative != 0;
}

bool canvasPointerOver(const Canvas& c) {
    return c.state != nullptr && (c.state->hovered != kUiNone || c.state->capture != kUiNone);
}

void canvasMaskScene(const Canvas& c, InputFrame& input) {
    if (c.state == nullptr) {
        return;
    }
    const UiState& s = *c.state;
    if (s.hovered != kUiNone || s.capture != kUiNone) {
        input.pointer.dx = 0.0f;
        input.pointer.dy = 0.0f;
        input.pointer.down = 0;
        input.pointer.pressed = 0;
        input.pointer.released = 0;
        input.clicks = 0;
    }
    if (s.ateWheel != 0) {
        input.wheel = 0.0f;
        input.wheelX = 0.0f;
    }
    if (s.focused != kUiNone && s.focused < s.count && s.ent[s.focused].try_get<UiField>() != nullptr) {
        input.textLen = 0;
        input.backspace = 0;
        input.del = 0;
        input.enter = 0;
        input.tab = 0;
        input.left = 0;
        input.right = 0;
        input.up = 0;
        input.down = 0;
        std::memset(input.keyDown, 0, sizeof(input.keyDown));
        std::memset(input.keyEdge, 0, sizeof(input.keyEdge));
        std::memset(input.keyUp, 0, sizeof(input.keyUp));
    }
}

bool canvasFocusBox(const Canvas& c, float& x, float& y, float& w, float& h) {
    if (c.state == nullptr || c.state->focused == kUiNone || c.state->focused >= c.state->count) {
        return false;
    }
    const UiBox& box = c.state->box[c.state->focused];
    x = box.x;
    y = box.y;
    w = box.w;
    h = box.h;
    return w > 0.0f && h > 0.0f;
}

void canvasSetDpi(Canvas& c, float dpi) {
    if (c.state == nullptr) {
        return;
    }
    if (dpi < 0.5f) {
        dpi = 1.0f;
    }
    if (c.state->dpi != dpi) {
        c.state->layoutDirty = true;
        c.state->visualDirty = true;
    }
    c.state->dpi = dpi;
}

bool canvasMouseClip(const Canvas& c, float& x, float& y, float& w, float& h) {
    if (c.state == nullptr || c.state->clipOn == 0) {
        return false;
    }
    x = c.state->clipBox.x;
    y = c.state->clipBox.y;
    w = c.state->clipBox.w;
    h = c.state->clipBox.h;
    return w > 0.0f && h > 0.0f;
}

void canvasApplyDpi(Canvas& c, float dpi, float previous) {
    if (c.state == nullptr || previous < 0.1f || dpi < 0.1f) {
        return;
    }
    (void)canvasUseFont(c, c.state->font.fontPx * (dpi / previous));
}

void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc, bool keepColor) {
    uiRendererRecord(cmd, c.rhi, sc, fc, c.rhi.primCount, keepColor);
}

} // namespace burnhope
