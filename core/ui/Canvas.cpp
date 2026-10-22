#include "ui/Canvas.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/Font.hpp"
#include "ui/State.hpp"
#include "platform/Trace.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>

namespace burnhope {

bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps) {
    canvasDestroy(c, d);
    c.state = std::make_unique<UiState>();
    c.device = &d;
    c.heaps = &heaps;

    if (!uiRendererCreate(c.rhi, d, heaps)) {
        canvasDestroy(c, d);
        return false;
    }

    c.photoMeta[0].frames = 2;
    c.photoMeta[0].delayMs[0] = 420;
    c.photoMeta[0].delayMs[1] = 420;
    for (uint8_t slot = 1; slot < kPhotoSlots; ++slot) {
        c.photoMeta[slot].frames = 1;
        c.photoMeta[slot].delayMs[0] = 100;
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
        c.state.reset();
    }
    c.device = nullptr;
    c.heaps = nullptr;
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

void canvasReload(Canvas& c) {
    if (c.state == nullptr) {
        return;
    }
    uiClear(*c.state);
    if (c.builder != nullptr) {
        c.builder(c, c.builderUser);
    }
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

bool canvasLoadPhoto(Canvas& c, uint8_t slot, const char* path) {
    if (slot >= kPhotoSlots || c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    // Вызов чистой инкапсулированной функции из UiPhoto.cpp
    const bool ok = uiPhotoLoad(c.rhi, *c.device, *c.heaps, c.photoMeta[slot], slot, path);
    if (ok && c.state != nullptr) {
        c.state->visualDirty = true;
    }
    return ok;
}

const PhotoMeta* canvasPhotoMeta(const Canvas& c, uint8_t slot) {
    if (slot >= kPhotoSlots) {
        return nullptr;
    }
    return &c.photoMeta[slot];
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
    if (!fontLoadSdf(c.state->font, c.state->look, *c.device, *c.heaps, c.rhi.font, path, pixelSize)) {
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
    if (c.state == nullptr || c.device == nullptr || c.heaps == nullptr) {
        return false;
    }
    if (!fontLoadDefaultSdf(c.state->font, c.state->look, *c.device, *c.heaps, c.rhi.font, pixelSize)) {
        return false;
    }
    uiRendererUploadFontTable(c.rhi, *c.device, c.state->font);
    uiRestyle(*c.state);
    return true;
}

UiEvent canvasConsume(
    Canvas& c,
    Device& d,
    float w,
    float h,
    const InputFrame& input,
    float timeSec,
    uint32_t flight,
    bool upload) {
    if (c.state == nullptr || c.state->count == 0 || w < 1.0f || h < 1.0f) {
        return UiEvent::None;
    }
    UiState& s = *c.state;
    s.time = timeSec;
    uiRevealTick(s);
    const float dpi = s.dpi < 0.5f ? 1.0f : s.dpi;
    const float pointsW = w / dpi;
    const float pointsH = h / dpi;

    InputFrame local = input;
    local.pointer.x /= dpi;
    local.pointer.y /= dpi;
    local.pointer.dx /= dpi;
    local.pointer.dy /= dpi;

    if (s.beforeLayout != nullptr) {
        s.beforeLayout(s);
    }
    // 1. Верстка дерева через Yoga
    uiLayout(s, pointsW, pointsH);

    // 2. Доставка событий ввода сущностям
    const UiEvent ev = uiApplyInput(s, local);
    if (s.layoutDirty) {
        uiLayout(s, pointsW, pointsH);
    }

    // 3. Анимации цвета и GIF
    uiPhotoTick(s);
    if (uiTickMotion(s)) {
        s.visualDirty = true;
    }

    // 4. Сборка вертексов в SSBO буфер слота kUiDescPrims
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
    if (c.state->focused == kUiNone || c.state->focused >= c.state->count) {
        return false;
    }
    return c.state->ent[c.state->focused].has<UiTextInput>();
}

bool canvasWantsRelative(const Canvas& c) {
    return c.state != nullptr && c.state->relative != 0;
}

bool canvasPointerOver(const Canvas& c) {
    return c.state != nullptr && (c.state->hovered != kUiNone || c.state->capture != kUiNone);
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