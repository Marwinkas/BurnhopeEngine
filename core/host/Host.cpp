#include "host/Host.hpp"
#include "platform/Jobs.hpp"

#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace burnhope {
namespace {

bool sameName(const HostView& view, const char* name) {
    return name != nullptr && name[0] != '\0' && std::strcmp(view.name, name) == 0;
}

void copyName(HostView& view, const char* name) {
    view.name[0] = '\0';
    if (name == nullptr) {
        return;
    }
    std::strncpy(view.name, name, sizeof(view.name) - 1);
    view.name[sizeof(view.name) - 1] = '\0';
}

bool extIs(const char* path, const char* ext) {
    const size_t n = std::strlen(path);
    const size_t m = std::strlen(ext);
    if (n < m) {
        return false;
    }
    for (size_t i = 0; i < m; ++i) {
        char a = path[n - m + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool isImagePath(const char* path) {
    return extIs(path, ".png") || extIs(path, ".jpg") || extIs(path, ".jpeg") || extIs(path, ".gif")
        || extIs(path, ".bmp") || extIs(path, ".tga") || extIs(path, ".hdr") || extIs(path, ".psd");
}

void acceptPath(Host& host, int slot, const char* path) {
    if (slot < 0 || slot >= kHostSlots || path == nullptr || path[0] == '\0') {
        return;
    }
    HostView& view = host.view[slot];
    if (!view.on) {
        return;
    }
    canvasOfferFile(view.canvas, path);
    if (isImagePath(path)) {
        (void)canvasLoadImage(view.canvas, 0, path);
    } else if (extIs(path, ".json")) {
        (void)canvasLoadJsonDoc(view.canvas, path);
    }
}

void destroyView(Host& host, int slot, bool notify) {
    if (slot < 0 || slot >= kHostSlots) {
        return;
    }
    HostView& view = host.view[slot];
    if (!view.on && view.window.handle == nullptr && view.surface == VK_NULL_HANDLE) {
        return;
    }
    windowRemember(view.window, view.name);
    HostCloseFn onClose = view.onClose;
    void* user = view.user;
    view.on = false;
    view.onOps = nullptr;
    view.onClose = nullptr;
    canvasDestroy(view.canvas, host.device);
    descriptorHeapsDestroy(view.heaps, host.device);
    swapchainDestroy(view.swap, host.device);
    if (view.surface != VK_NULL_HANDLE && host.device.instance != VK_NULL_HANDLE) {
        if (view.surface == host.device.surface) {
            host.device.surface = VK_NULL_HANDLE;
        }
        vkDestroySurfaceKHR(host.device.instance, view.surface, nullptr);
        view.surface = VK_NULL_HANDLE;
    }
    windowDestroy(view.window);
    view.name[0] = '\0';
    view.book = nullptr;
    view.user = nullptr;
    if (host.fileSlot == slot) {
        host.fileSlot = -1;
    }
    if (notify && onClose != nullptr) {
        onClose(host, slot, user);
    }
}

// Фаза 1 (последовательно, до любых потоков): DPI и resize. swapchainDestroy ждёт
// fence только этого окна, затем пул кадра сбрасывается целиком.
// Возвращает false только если primary не смог пересобраться (фатально для hostTick).
bool prepareViewSync(Host& host, int slot) {
    HostView& view = host.view[slot];
    if (view.window.pixelW == 0 || view.window.pixelH == 0) {
        return true;
    }
    canvasSetDpi(view.canvas, view.window.dpi);
    if (view.window.dpiChanged) {
        canvasApplyDpi(view.canvas, view.window.dpi, view.window.dpiApplied);
        view.window.dpiApplied = view.window.dpi;
        view.window.dpiChanged = false;
    }
    if (view.window.resized) {
        bool busy = false;
        if (!deviceLost(host.device)) {
            for (uint32_t i = 0; i < kFramesInFlight; ++i) {
                if (view.swap.flightFence[i] == VK_NULL_HANDLE) {
                    continue;
                }
                if (vkGetFenceStatus(host.device.device, view.swap.flightFence[i]) != VK_SUCCESS) {
                    busy = true;
                    break;
                }
            }
        }
        if (busy) {
            if (view.swap.gpuBusy == 0) {
                spdlog::warn("resize deferred, gpu busy");
            }
            view.swap.gpuBusy = 1;
        } else {
        view.window.resized = false;
        view.swap.gpuBusy = 0;
        windowRefreshSize(view.window);
        if (view.window.pixelW == 0 || view.window.pixelH == 0) {
            return true;
        }
        if (!swapchainRecreate(view.swap, host.device, view.window.pixelW, view.window.pixelH)) {
            spdlog::error("resize recreate failed");
            if (slot == host.primary) {
                return false;
            }
            destroyView(host, slot, true);
            return true;
        }
        }
    }
    if (view.window.input.f12) {
        rhiCaptureToggle();
    }
    if ((view.window.input.pointer.pressed & kPointerLeft) != 0) {
        view.swap.clickTick = platformCounter();
    }
    float title = 40.0f;
    float border = 6.0f;
    windowChrome(view.window, title, border);
    return true;
}

// Результат параллельной фазы одного окна. SDL-вызовов и app-callback (onOps) здесь нет —
// только Vulkan-запись в свой пул/буфер (Фаза 1.5) и чтение Canvas того же окна.
struct DrawJobResult {
    bool handled = false;
    bool needsResize = false;
    UiEvent ev = UiEvent::None;
    WindowOps ops{};
    char clip[96]{};
    bool hasClip = false;
    int cursor = 0;
    bool wantsText = false;
    bool wantsRelative = false;
    bool hasClipRect = false;
    float clipX = 0, clipY = 0, clipW = 0, clipH = 0;
    bool hasFocus = false;
    float focusX = 0, focusY = 0, focusW = 0, focusH = 0;
};

struct DrawJobCtx {
    Host* host = nullptr;
    int slot = 0;
    float now = 0.0f;
    DrawJobResult result{};
};

// Фаза 2 (может идти на рабочем потоке job system, core/platform/Jobs.hpp): acquire,
// canvasConsume (layout/hit-test — самая дорогая часть по DOD-аудиту), запись и
// submit+present. Каждое окно пишет только в свой Swapchain.pool/cmd и свой Canvas —
// общая очередь и общий upload-пул защищены Device::queueMutex внутри
// swapchainSubmitPresent/submitImage. SDL и onOps сюда не попадают — их нельзя звать не с
// главного потока (SDL) и нельзя звать параллельно друг другу (onOps может реентерантно
// дёргать hostOpen/hostClose, которые мутируют общий Host).
void recordAndPresentView(DrawJobCtx& ctx) {
    Host& host = *ctx.host;
    HostView& view = host.view[ctx.slot];
    DrawJobResult& out = ctx.result;
    if (view.window.pixelW == 0 || view.window.pixelH == 0) {
        return;
    }
    const float dt = host.frameDt;
    if (view.sceneTick != nullptr) {
        out.wantsRelative = view.sceneTick(view.user, view.window.input, dt);
    }
    const bool flyLook = out.wantsRelative;
    FrameContext frame{};
    if (!swapchainBegin(view.swap, host.device, frame)) {
        if (view.swap.gpuBusy != 0) {
            // Кадр на GPU ещё занят. Взгляд уже сдвинут этим тиком, картинка догонит следующим present.
            if (view.sceneRecord != nullptr) {
                out.wantsRelative = flyLook;
            }
            out.handled = true;
            return;
        }
        out.needsResize = true;
        return;
    }
    if (view.window.input.reload) {
        canvasReload(view.canvas);
    }
    float title = view.sceneRecord != nullptr ? 0.0f : 40.0f;
    float border = view.sceneRecord != nullptr ? 0.0f : 6.0f;
    const bool sceneView = view.sceneRecord != nullptr;
    if (view.sceneRecord != nullptr) {
        heapBind(frame.cmd, view.heaps, frame.flight);
        view.sceneRecord(view.user, frame.cmd, host.device, view.heaps, view.swap, frame);
    }
    if (view.sceneRecord != nullptr && view.canvas.builder == nullptr) {
        swapchainToPresent(view.swap, frame);
        if (!swapchainSubmitPresent(view.swap, host.device, frame, ctx.slot == host.primary)) {
            out.needsResize = true;
            return;
        }
        out.handled = true;
        return;
    }
    if (!sceneView) {
        canvasChrome(view.canvas, title, border);
    }
    char dropped[260]{};
    if (view.window.input.dropLen > 0) {
        std::memcpy(dropped, view.window.input.drop, view.window.input.dropLen + 1u);
        acceptPath(host, ctx.slot, dropped);
    }
    out.ev = canvasConsume(
        view.canvas,
        host.device,
        static_cast<float>(view.swap.extent.width),
        static_cast<float>(view.swap.extent.height),
        view.window.input,
        ctx.now);
    out.hasClip = canvasTakeClip(view.canvas, out.clip, sizeof(out.clip));
    canvasTakeWindowOps(view.canvas, out.ops);
    out.cursor = canvasCursor(view.canvas);
    out.wantsText = canvasWantsText(view.canvas);
    out.wantsRelative = canvasWantsRelative(view.canvas);
    out.hasClipRect = canvasMouseClip(view.canvas, out.clipX, out.clipY, out.clipW, out.clipH);
    out.hasFocus = canvasFocusBox(view.canvas, out.focusX, out.focusY, out.focusW, out.focusH);
    if (sceneView) {
        out.wantsRelative = flyLook;
        out.ops = {};
        out.wantsText = false;
        out.hasClip = false;
        out.cursor = 0;
    }

    // Тонмап чистит свопчейн в буфере сцены. Холст — следующий сабмит той же очереди:
    // ждёт uiSem, грузит картинку (LOAD) и только он переводит её в present.
    // Сцена в present не уходит, иначе на экране остаётся clear без панели.
    if (sceneView && swapchainBeginUi(view.swap, frame)) {
        heapBind(frame.uiCmd, view.heaps, frame.flight);
        canvasRecord(frame.uiCmd, view.canvas, view.swap, frame, true);
    } else {
        heapBind(frame.cmd, view.heaps, frame.flight);
        canvasRecord(frame.cmd, view.canvas, view.swap, frame, sceneView);
    }
    swapchainToPresent(view.swap, frame);
    if (!swapchainSubmitPresent(view.swap, host.device, frame, ctx.slot == host.primary)) {
        out.needsResize = true;
        return;
    }
    out.handled = true;
}

void recordAndPresentViewTrampoline(void* user) {
    recordAndPresentView(*static_cast<DrawJobCtx*>(user));
}

// Фаза 3 (последовательно, главный поток): всё SDL (clipboard/cursor/IME/resize-окна/
// file-dialog) и onOps — ровно то, что обязано остаться на главном потоке. Отрисовка уже
// произошла в фазе 2; здесь только эпилог. Отличие от прежнего порядка: кадр уже
// отправлен в present до вызова onOps/close, так что окно, которое onOps закрывает в этот
// же тик, один раз успеет отрисоваться лишним кадром — это не портит данные (swapchain
// destroy сам ждёт device idle), просто косметика на один кадр при закрытии.
void finishView(Host& host, int slot, const DrawJobResult& r) {
    HostView& view = host.view[slot];
    if (r.needsResize) {
        view.window.resized = true;
    }
    if (!r.handled) {
        return;
    }
    if (r.hasClip) {
        windowClipboardSet(r.clip);
    }
    WindowCommand command{};
    command.minimize = r.ops.minimize;
    command.toggleMaximize = r.ops.toggleMaximize;
    command.alwaysOnTop = r.ops.alwaysOnTop;
    command.opacity = r.ops.opacity;
    windowApply(view.window, command);
    windowSetCursor(view.window, r.cursor);
    windowText(view.window, r.wantsText);
    if (r.hasClipRect) {
        windowMouseRect(view.window, static_cast<int>(r.clipX), static_cast<int>(r.clipY), static_cast<int>(r.clipW), static_cast<int>(r.clipH));
    } else {
        windowMouseRect(view.window, 0, 0, 0, 0);
    }
    windowRelativeMouse(view.window, r.wantsRelative);
    if (r.hasFocus && view.window.pixelW > 0 && view.swap.extent.width > 0) {
        const float sx = static_cast<float>(view.window.pixelW) / static_cast<float>(view.swap.extent.width);
        windowTextArea(
            view.window,
            static_cast<int>(r.focusX * sx),
            static_cast<int>(r.focusY * sx),
            static_cast<int>(r.focusW * sx),
            static_cast<int>(r.focusH * sx));
    }
    WindowOps ops = r.ops;
    if (ops.openFile) {
        static const FileFilter filters[] = {
            {"All files", "*"},
            {"JSON", "json"},
            {"Images", "png;jpg;jpeg;gif;bmp;tga;hdr;psd"},
        };
        if (windowAskFile(view.window, filters, 3, FileAsk::Open)) {
            host.fileSlot = slot;
        }
        ops.openFile = false;
    }
    if (r.ev == UiEvent::Close || ops.close) {
        if (slot == host.primary) {
            view.window.closeRequested = true;
        } else {
            destroyView(host, slot, true);
            return;
        }
    }
    if (!view.on) {
        return;
    }
    if (view.onOps != nullptr) {
        view.onOps(host, slot, ops, view.user);
    }
}

const char* actionPath() {
    static char path[512];
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return nullptr;
    }
    std::snprintf(path, sizeof(path), "%s/.cache/burnhope-actions.txt", home);
    return path;
}

} // namespace

bool hostInit(Host& host) {
    if (!frameArenaCreate(host.arena, 1 * 1024 * 1024)) {
        spdlog::error("FrameArena 1MB failed");
        return false;
    }
    if (const char* path = actionPath()) {
        (void)actionLoad(path);
    }
    host.ready = true;
    return true;
}

void hostShutdown(Host& host) {
    if (host.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(host.device.device);
    }
    if (const char* path = actionPath()) {
        (void)actionSave(path);
    }
    for (int i = 0; i < kHostSlots; ++i) {
        if (host.view[i].on || host.view[i].window.handle != nullptr) {
            destroyView(host, i, false);
        }
    }
    frameArenaDestroy(host.arena);
    deviceDestroy(host.device);
    platformShutdown();
    host.primary = -1;
    host.ready = false;
}

int hostOpen(Host& host, const ViewDesc& desc) {
    if (desc.name != nullptr && desc.name[0] != '\0') {
        for (int i = 0; i < kHostSlots; ++i) {
            if (host.view[i].on && sameName(host.view[i], desc.name)) {
                windowRaise(host.view[i].window);
                return i;
            }
        }
    }
    int slot = -1;
    for (int i = 0; i < kHostSlots; ++i) {
        if (!host.view[i].on) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    HostView& view = host.view[slot];
    WindowConfig cfg = desc.window;
    windowRecall(cfg, desc.name);
    if (!windowCreate(view.window, cfg)) {
        spdlog::error("windowCreate failed: {}", platformError());
        return -1;
    }
    if (cfg.maximized) {
        windowMaximize(view.window);
    }
    const bool first = host.device.instance == VK_NULL_HANDLE;
    if (first) {
        if (!deviceCreate(host.device, view.window)) {
            windowDestroy(view.window);
            return -1;
        }
        view.surface = host.device.surface;
        host.primary = slot;
    } else if (!deviceCreateSurface(host.device, view.window, view.surface)) {
        spdlog::error("surface failed: {}", platformError());
        windowDestroy(view.window);
        return -1;
    }
    FrameDesc frame = desc.frame;
    if (desc.window.transparent) {
        frame.transparent = true;
    }
    view.frame = frame;
    if (!swapchainCreate(view.swap, host.device, view.surface, view.window.pixelW, view.window.pixelH, frame)) {
        destroyView(host, slot, false);
        return -1;
    }
    const HeapLayout heap{.buffers = 64, .images = 810, .samplers = 3};
    if (!descriptorHeapsCreate(view.heaps, host.device, heap) || !canvasCreate(view.canvas, host.device, view.heaps)) {
        destroyView(host, slot, false);
        return -1;
    }
    if (desc.book != nullptr) {
        canvasSetBook(view.canvas, desc.book);
    }
    canvasSetBuilder(view.canvas, desc.builder, desc.user);
    canvasReload(view.canvas);
    copyName(view, desc.name);
    view.book = desc.book;
    view.onOps = desc.onOps;
    view.onClose = desc.onClose;
    view.sceneTick = desc.sceneTick;
    view.sceneRecord = desc.sceneRecord;
    view.user = desc.user;
    view.on = true;
    const char* present = "vsync";
    if (frame.present == Present::Mailbox) {
        present = "mailbox";
    } else if (frame.present == Present::Immediate) {
        present = "immediate";
    } else if (frame.present == Present::Relaxed) {
        present = "relaxed";
    }
    spdlog::info("window opened {} present {}", view.name[0] != '\0' ? view.name : "id", present);
    return slot;
}

void hostClose(Host& host, int slot) {
    destroyView(host, slot, true);
}

bool hostSetFrame(Host& host, int slot, const FrameDesc& frame) {
    if (slot < 0 || slot >= kHostSlots || !host.view[slot].on) {
        return false;
    }
    HostView& view = host.view[slot];
    if (view.frame.present == frame.present && view.frame.transparent == frame.transparent && view.frame.clipped == frame.clipped) {
        return true;
    }
    view.frame = frame;
    view.swap.asked = frame;
    if (view.window.pixelW == 0 || view.window.pixelH == 0 || host.device.device == VK_NULL_HANDLE) {
        return true;
    }
    return swapchainRecreate(view.swap, host.device, view.window.pixelW, view.window.pixelH);
}

FrameDesc hostFrame(const Host& host, int slot) {
    if (slot < 0 || slot >= kHostSlots || !host.view[slot].on) {
        return {};
    }
    return host.view[slot].frame;
}

void releaseViewGpu(Host& host, int slot) {
    HostView& view = host.view[slot];
    const UiBuilder builder = view.canvas.builder;
    void* builderUser = view.canvas.builderUser;
    canvasDestroy(view.canvas, host.device);
    descriptorHeapsDestroy(view.heaps, host.device);
    swapchainDestroy(view.swap, host.device);
    if (view.surface != VK_NULL_HANDLE && host.device.instance != VK_NULL_HANDLE) {
        if (view.surface == host.device.surface) {
            host.device.surface = VK_NULL_HANDLE;
        }
        vkDestroySurfaceKHR(host.device.instance, view.surface, nullptr);
        view.surface = VK_NULL_HANDLE;
    }
    view.swap = {};
    view.heaps = {};
    view.canvas = {};
    view.canvas.builder = builder;
    view.canvas.builderUser = builderUser;
}

bool restoreViewGpu(Host& host, int slot, bool ownsDeviceSurface) {
    HostView& view = host.view[slot];
    if (ownsDeviceSurface) {
        view.surface = host.device.surface;
    } else if (!deviceCreateSurface(host.device, view.window, view.surface)) {
        spdlog::error("HOST recover surface failed {}", view.name);
        return false;
    }
    if (!swapchainCreate(view.swap, host.device, view.surface, view.window.pixelW, view.window.pixelH, view.frame)) {
        spdlog::error("HOST recover swapchain failed {}", view.name);
        return false;
    }
    const HeapLayout heap{.buffers = 64, .images = 810, .samplers = 3};
    if (!descriptorHeapsCreate(view.heaps, host.device, heap) || !canvasCreate(view.canvas, host.device, view.heaps)) {
        spdlog::error("HOST recover canvas failed {}", view.name);
        return false;
    }
    if (view.book != nullptr) {
        canvasSetBook(view.canvas, view.book);
    }
    canvasSetBuilder(view.canvas, view.canvas.builder, view.canvas.builderUser);
    canvasReload(view.canvas);
    return true;
}

// Main thread only. Workers set Device::lost and return; this rebuilds the device
// and every open window's swapchain/canvas. Windows themselves stay.
bool hostRecover(Host& host) {
    if (host.primary < 0 || !host.view[host.primary].on) {
        spdlog::error("HOST recover: no primary window");
        return false;
    }
    spdlog::error("HOST recover 1: new draws are no-ops");
    if (host.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(host.device.device);
    }
    if (host.releaseGpu != nullptr) {
        host.releaseGpu(host.releaseUser, host.device);
    }
    spdlog::error("HOST recover 2: release swapchains, keep windows");
    for (int i = 0; i < kHostSlots; ++i) {
        if (host.view[i].on) {
            releaseViewGpu(host, i);
        }
    }
    spdlog::error("HOST recover 3: recreate logical device");
    if (!deviceCreate(host.device, host.view[host.primary].window)) {
        spdlog::error("HOST recover: deviceCreate failed");
        return false;
    }
    spdlog::error("HOST recover 4: recreate each window");
    if (!restoreViewGpu(host, host.primary, true)) {
        return false;
    }
    for (int i = 0; i < kHostSlots; ++i) {
        if (i == host.primary || !host.view[i].on) {
            continue;
        }
        if (!restoreViewGpu(host, i, false)) {
            destroyView(host, i, true);
        }
    }
    host.device.lost.store(0, std::memory_order_release);
    spdlog::error("HOST recover 5: device live");
    return true;
}

bool hostTick(Host& host) {
    Window* list[kHostSlots]{};
    int count = 0;
    if (host.primary >= 0 && host.view[host.primary].on) {
        list[count++] = &host.view[host.primary].window;
    }
    for (int i = 0; i < kHostSlots; ++i) {
        if (i == host.primary || !host.view[i].on) {
            continue;
        }
        list[count++] = &host.view[i].window;
    }
    if (count == 0) {
        return false;
    }
    windowsPump(list, count);
    if (deviceLost(host.device)) {
        spdlog::error("HOST device lost — recover before the next draw");
        if (!hostRecover(host)) {
            return false;
        }
    }
    frameArenaReset(host.arena);
    char path[512]{};
    if (windowTakeFile(path, static_cast<int>(sizeof(path)))) {
        acceptPath(host, host.fileSlot, path);
    }
    const float now = platformSeconds();
    float dt = host.clock > 0.0f ? now - host.clock : 0.0f;
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    host.clock = now;
    host.frameDt = dt;
    for (int i = 0; i < kHostSlots; ++i) {
        if (i != host.primary && host.view[i].on && host.view[i].window.closeRequested) {
            destroyView(host, i, true);
        }
    }
    // Фаза 1: DPI/resize, строго по порядку, на главном потоке (см. prepareViewSync).
    if (host.primary >= 0 && host.view[host.primary].on) {
        if (!prepareViewSync(host, host.primary)) {
            return false;
        }
    }
    for (int i = 0; i < kHostSlots; ++i) {
        if (i == host.primary || !host.view[i].on) {
            continue;
        }
        prepareViewSync(host, i);
    }
    // Фаза 2: acquire+consume+record+submit+present всех открытых окон параллельно
    // (core/platform/Jobs.hpp). Каждое окно — свой Swapchain.pool/cmd/Canvas; общая
    // очередь/upload-пул защищены Device::queueMutex. При одном открытом окне (обычный
    // случай) jobsRunAndWait(count=1) просто зовёт функцию напрямую, без потоков.
    DrawJobCtx ctx[kHostSlots];
    Job jobs[kHostSlots];
    int jobCount = 0;
    int jobSlot[kHostSlots];
    if (host.primary >= 0 && host.view[host.primary].on) {
        ctx[jobCount] = DrawJobCtx{&host, host.primary, now, {}};
        jobSlot[jobCount] = host.primary;
        jobs[jobCount] = Job{&recordAndPresentViewTrampoline, &ctx[jobCount]};
        ++jobCount;
    }
    for (int i = 0; i < kHostSlots; ++i) {
        if (i == host.primary || !host.view[i].on) {
            continue;
        }
        ctx[jobCount] = DrawJobCtx{&host, i, now, {}};
        jobSlot[jobCount] = i;
        jobs[jobCount] = Job{&recordAndPresentViewTrampoline, &ctx[jobCount]};
        ++jobCount;
    }
    jobsRunAndWait(jobs, jobCount);
    // Фаза 3: SDL + onOps, строго последовательно, в прежнем порядке (primary первым).
    for (int j = 0; j < jobCount; ++j) {
        finishView(host, jobSlot[j], ctx[j].result);
    }
    bool live = false;
    for (int i = 0; i < kHostSlots; ++i) {
        if (host.view[i].on && host.view[i].window.focused && host.view[i].window.pixelW > 0) {
            live = true;
            break;
        }
    }
    if (!live) {
        platformSleep(32);
    } else if (host.primary >= 0) {
        platformPace(host.view[host.primary].frame.hz);
    }
    return true;
}

bool hostQuit(const Host& host) {
    return host.primary >= 0 && host.view[host.primary].window.closeRequested;
}

Canvas* hostCanvas(Host& host, int slot) {
    if (slot < 0 || slot >= kHostSlots || !host.view[slot].on) {
        return nullptr;
    }
    return &host.view[slot].canvas;
}

} // namespace burnhope
