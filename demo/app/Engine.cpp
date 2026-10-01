#include "app/Engine.hpp"
#include "app/Shell.hpp"

#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_vulkan.h>

namespace {

std::atomic<uint32_t> gFileGen{0};
std::atomic<int> gDialogOn{0};
char gFilePath[512]{};
char gDialogDir[512]{};
uint32_t gFileSeen = 0;

void loadDialogDir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }
    char path[640]{};
    std::snprintf(path, sizeof(path), "%s/.cache/burnhope-dialog.txt", home);
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return;
    }
    if (std::fgets(gDialogDir, sizeof(gDialogDir), file) != nullptr) {
        size_t n = std::strlen(gDialogDir);
        while (n > 0 && (gDialogDir[n - 1] == '\n' || gDialogDir[n - 1] == '\r')) {
            gDialogDir[--n] = '\0';
        }
    }
    std::fclose(file);
}

void rememberFolder(const char* file) {
    const char* slash = std::strrchr(file, '/');
    if (slash == nullptr || slash == file) {
        return;
    }
    const size_t n = static_cast<size_t>(slash - file);
    if (n >= sizeof(gDialogDir)) {
        return;
    }
    std::memcpy(gDialogDir, file, n);
    gDialogDir[n] = '\0';
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }
    char path[640]{};
    std::snprintf(path, sizeof(path), "%s/.cache/burnhope-dialog.txt", home);
    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
        return;
    }
    std::fputs(gDialogDir, out);
    std::fputc('\n', out);
    std::fclose(out);
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

void onFileChosen(void*, const char* const* list, int) {
    if (list != nullptr && list[0] != nullptr) {
        std::strncpy(gFilePath, list[0], sizeof(gFilePath) - 1);
        gFilePath[sizeof(gFilePath) - 1] = '\0';
    } else {
        gFilePath[0] = '\0';
    }
    gDialogOn.store(0, std::memory_order_release);
    gFileGen.fetch_add(1, std::memory_order_release);
}

} // namespace

namespace burnhope {

void engineToolClose(Engine& e) {
    if (e.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(e.device.device);
    }
    canvasDestroy(e.toolCanvas, e.device);
    descriptorHeapsDestroy(e.toolHeaps, e.device);
    swapchainDestroy(e.toolSwap, e.device);
    if (e.toolSurface != VK_NULL_HANDLE && e.device.instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(e.device.instance, e.toolSurface, nullptr);
        e.toolSurface = VK_NULL_HANDLE;
    }
    windowDestroy(e.tool);
    e.toolOn = false;
}

bool engineToolOpen(Engine& e) {
    if (e.toolOn && e.tool.handle != nullptr) {
        SDL_RaiseWindow(e.tool.handle);
        return true;
    }
    WindowConfig config{};
    config.title = "Burnhope";
    config.width = 560;
    config.height = 420;
    config.minWidth = 360;
    config.minHeight = 240;
    config.bordered = false;
    config.resizable = true;
    if (!windowCreate(e.tool, config)) {
        spdlog::error("tool window failed: {}", SDL_GetError());
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(e.tool.handle, e.device.instance, nullptr, &e.toolSurface)) {
        spdlog::error("tool surface failed: {}", SDL_GetError());
        windowDestroy(e.tool);
        return false;
    }
    if (!swapchainCreate(e.toolSwap, e.device, e.toolSurface, e.tool.pixelW, e.tool.pixelH)) {
        engineToolClose(e);
        return false;
    }
    const HeapLayout heap{.buffers = 1, .images = 9, .samplers = 1};
    if (!descriptorHeapsCreate(e.toolHeaps, e.device, heap) || !canvasCreate(e.toolCanvas, e.device, e.toolHeaps)) {
        engineToolClose(e);
        return false;
    }
    canvasSetBuilder(e.toolCanvas, toolBuild, nullptr);
    canvasReload(e.toolCanvas);
    e.toolOn = true;
    spdlog::info("os window opened");
    return true;
}

void engineColorClose(Engine& e) {
    if (e.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(e.device.device);
    }
    canvasDestroy(e.colorCanvas, e.device);
    descriptorHeapsDestroy(e.colorHeaps, e.device);
    swapchainDestroy(e.colorSwap, e.device);
    if (e.colorSurface != VK_NULL_HANDLE && e.device.instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(e.device.instance, e.colorSurface, nullptr);
        e.colorSurface = VK_NULL_HANDLE;
    }
    windowDestroy(e.colorWin);
    e.colorOn = false;
    e.colors.present = 0;
    ++e.colors.gen;
}

bool engineColorOpen(Engine& e) {
    if (e.colorOn && e.colorWin.handle != nullptr) {
        SDL_RaiseWindow(e.colorWin.handle);
        return true;
    }
    WindowConfig config{};
    config.title = "Color";
    config.width = 440;
    config.height = 720;
    config.minWidth = 360;
    config.minHeight = 480;
    config.bordered = false;
    config.resizable = true;
    if (!windowCreate(e.colorWin, config)) {
        spdlog::error("color window failed: {}", SDL_GetError());
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(e.colorWin.handle, e.device.instance, nullptr, &e.colorSurface)) {
        spdlog::error("color surface failed: {}", SDL_GetError());
        windowDestroy(e.colorWin);
        return false;
    }
    if (!swapchainCreate(e.colorSwap, e.device, e.colorSurface, e.colorWin.pixelW, e.colorWin.pixelH)) {
        engineColorClose(e);
        return false;
    }
    const HeapLayout heap{.buffers = 1, .images = 9, .samplers = 1};
    if (!descriptorHeapsCreate(e.colorHeaps, e.device, heap) || !canvasCreate(e.colorCanvas, e.device, e.colorHeaps)) {
        engineColorClose(e);
        return false;
    }
    canvasSetBook(e.colorCanvas, &e.colors);
    canvasSetBuilder(e.colorCanvas, colorBuild, nullptr);
    canvasReload(e.colorCanvas);
    e.colorOn = true;
    e.colors.mode = 1;
    e.colors.present = 2;
    spdlog::info("color window opened");
    return true;
}

bool engineInit(Engine& e, int argc, char** argv) {
    (void)argc;
    (void)argv;
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — Vulkan 1.4 core + Yoga/Flecs canvas.");
    loadDialogDir();

    WindowConfig windowConfig{};
    windowConfig.title = "Burnhope";
    windowConfig.width = 1600;
    windowConfig.height = 900;
    windowConfig.bordered = false;
    windowConfig.resizable = true;
    if (!windowCreate(e.window, windowConfig)) {
        spdlog::error("windowCreate failed: {}", SDL_GetError());
        return false;
    }
    if (!deviceCreate(e.device, e.window)) {
        return false;
    }
    if (!swapchainCreate(e.swap, e.device, e.window.pixelW, e.window.pixelH)) {
        return false;
    }
    const HeapLayout heap{.buffers = 1, .images = 9, .samplers = 1};
    if (!descriptorHeapsCreate(e.heaps, e.device, heap)) {
        return false;
    }
    if (!canvasCreate(e.canvas, e.device, e.heaps)) {
        return false;
    }
    canvasSetBook(e.canvas, &e.colors);
    canvasSetBuilder(e.canvas, shellBuild, nullptr);
    canvasReload(e.canvas);
    if (!frameArenaCreate(e.arena, 1 * 1024 * 1024)) {
        spdlog::error("FrameArena 1MB failed");
        return false;
    }
    return true;
}

void engineShutdown(Engine& e) {
    if (e.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(e.device.device);
    }
    if (e.toolOn) {
        engineToolClose(e);
    }
    if (e.colorOn) {
        engineColorClose(e);
    }
    frameArenaDestroy(e.arena);
    canvasDestroy(e.canvas, e.device);
    descriptorHeapsDestroy(e.heaps, e.device);
    swapchainDestroy(e.swap, e.device);
    deviceDestroy(e.device);
    windowDestroy(e.window);
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
        SDL_Quit();
    }
}

bool engineTick(Engine& e) {
    windowsPump(e.window, e.toolOn ? &e.tool : nullptr, e.colorOn ? &e.colorWin : nullptr);
    frameArenaReset(e.arena);

    auto recreate = [&]() -> bool {
        windowRefreshSize(e.window);
        if (e.window.pixelW == 0 || e.window.pixelH == 0) {
            return true;
        }
        vkDeviceWaitIdle(e.device.device);
        return swapchainRecreate(e.swap, e.device, e.window.pixelW, e.window.pixelH);
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

    if (e.window.input.reload) {
        canvasReload(e.canvas);
    }
    float titleBar = 40.0f;
    float resizeBorder = 6.0f;
    canvasChrome(e.canvas, titleBar, resizeBorder);
    e.window.titleBarPx = titleBar;
    e.window.resizeBorderPx = resizeBorder;
    const float now = static_cast<float>(SDL_GetTicks()) * 0.001f;
    const UiEvent ev = canvasConsume(
        e.canvas,
        e.device,
        static_cast<float>(e.swap.extent.width),
        static_cast<float>(e.swap.extent.height),
        e.window.input,
        now);
    char clip[96]{};
    if (canvasTakeClip(e.canvas, clip, 96)) {
        SDL_SetClipboardText(clip);
    }
    WindowOps ops{};
    canvasTakeWindowOps(e.canvas, ops);
    if (ev == UiEvent::Close || ops.close) {
        e.window.closeRequested = true;
    }
    if (ops.minimize && e.window.handle != nullptr) {
        SDL_MinimizeWindow(e.window.handle);
    }
    if (ops.toggleMaximize && e.window.handle != nullptr) {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(e.window.handle);
        if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
            SDL_RestoreWindow(e.window.handle);
        } else {
            SDL_MaximizeWindow(e.window.handle);
        }
    }
    if (ops.alwaysOnTop >= 0 && e.window.handle != nullptr) {
        SDL_SetWindowAlwaysOnTop(e.window.handle, ops.alwaysOnTop != 0);
    }
    if (ops.opacity >= 0.0f && e.window.handle != nullptr) {
        SDL_SetWindowOpacity(e.window.handle, ops.opacity);
    }
    windowSetCursor(e.window, canvasCursor(e.canvas));
    if (ops.openTool) {
        engineToolOpen(e);
    }
    if (ops.openColor) {
        engineColorOpen(e);
    }
    if (ops.openFile && gDialogOn.exchange(1, std::memory_order_acq_rel) == 0) {
        static const SDL_DialogFileFilter filters[] = {
            {"All files", "*"},
            {"JSON", "json"},
            {"Images", "png;jpg;jpeg;gif;bmp;tga;hdr;psd"},
        };
        const char* folder = gDialogDir[0] != '\0' ? gDialogDir : nullptr;
        SDL_ShowOpenFileDialog(onFileChosen, nullptr, e.window.handle, filters, 3, folder, false);
    }
    const uint32_t fileGen = gFileGen.load(std::memory_order_acquire);
    if (fileGen != gFileSeen) {
        gFileSeen = fileGen;
        if (gFilePath[0] != '\0') {
            canvasOfferFile(e.canvas, gFilePath);
            rememberFolder(gFilePath);
            if (isImagePath(gFilePath)) {
                (void)canvasLoadImage(e.canvas, 0, gFilePath);
            } else if (extIs(gFilePath, ".json")) {
                (void)canvasLoadJsonDoc(e.canvas, gFilePath);
            }
        }
    }
    if (canvasWantsText(e.canvas)) {
        SDL_StartTextInput(e.window.handle);
    } else {
        SDL_StopTextInput(e.window.handle);
    }

    heapBind(fc.cmd, e.heaps);
    canvasRecord(fc.cmd, e.canvas, e.swap, fc);
    swapchainToPresent(e.swap, fc);
    if (!swapchainSubmitPresent(e.swap, e.device, fc)) {
        return recreate();
    }

    if (e.toolOn && e.tool.handle != nullptr) {
        if (e.tool.closeRequested) {
            engineToolClose(e);
        } else if (e.tool.pixelW > 0 && e.tool.pixelH > 0) {
            if (e.tool.resized) {
                e.tool.resized = false;
                vkDeviceWaitIdle(e.device.device);
                if (!swapchainRecreate(e.toolSwap, e.device, e.tool.pixelW, e.tool.pixelH)) {
                    spdlog::error("tool resize failed");
                    engineToolClose(e);
                }
            }
            if (e.toolOn) {
                float title = 40.0f;
                float border = 6.0f;
                canvasChrome(e.toolCanvas, title, border);
                e.tool.titleBarPx = title;
                e.tool.resizeBorderPx = border;
                FrameContext toolFrame{};
                if (swapchainBegin(e.toolSwap, e.device, toolFrame)) {
                    if (e.tool.input.reload) {
                        canvasReload(e.toolCanvas);
                    }
                    const UiEvent toolEv = canvasConsume(
                        e.toolCanvas,
                        e.device,
                        static_cast<float>(e.toolSwap.extent.width),
                        static_cast<float>(e.toolSwap.extent.height),
                        e.tool.input,
                        now);
                    WindowOps toolOps{};
                    canvasTakeWindowOps(e.toolCanvas, toolOps);
                    char toolClip[96]{};
                    if (canvasTakeClip(e.toolCanvas, toolClip, 96)) {
                        SDL_SetClipboardText(toolClip);
                    }
                    if (toolEv == UiEvent::Close || toolOps.close) {
                        engineToolClose(e);
                    } else {
                        if (toolOps.minimize) {
                            SDL_MinimizeWindow(e.tool.handle);
                        }
                        if (toolOps.toggleMaximize) {
                            const SDL_WindowFlags flags = SDL_GetWindowFlags(e.tool.handle);
                            if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
                                SDL_RestoreWindow(e.tool.handle);
                            } else {
                                SDL_MaximizeWindow(e.tool.handle);
                            }
                        }
                        windowSetCursor(e.tool, canvasCursor(e.toolCanvas));
                        heapBind(toolFrame.cmd, e.toolHeaps);
                        canvasRecord(toolFrame.cmd, e.toolCanvas, e.toolSwap, toolFrame);
                        swapchainToPresent(e.toolSwap, toolFrame);
                        if (!swapchainSubmitPresent(e.toolSwap, e.device, toolFrame)) {
                            e.tool.resized = true;
                        }
                    }
                } else {
                    e.tool.resized = true;
                }
            }
        }
    }

    if (e.colorOn && e.colorWin.handle != nullptr) {
        if (e.colorWin.closeRequested) {
            engineColorClose(e);
        } else if (e.colorWin.pixelW > 0 && e.colorWin.pixelH > 0) {
            if (e.colorWin.resized) {
                e.colorWin.resized = false;
                vkDeviceWaitIdle(e.device.device);
                if (!swapchainRecreate(e.colorSwap, e.device, e.colorWin.pixelW, e.colorWin.pixelH)) {
                    spdlog::error("color resize failed");
                    engineColorClose(e);
                }
            }
            if (e.colorOn) {
                float title = 40.0f;
                float border = 6.0f;
                canvasChrome(e.colorCanvas, title, border);
                e.colorWin.titleBarPx = title;
                e.colorWin.resizeBorderPx = border;
                FrameContext colorFrame{};
                if (swapchainBegin(e.colorSwap, e.device, colorFrame)) {
                    const UiEvent colorEv = canvasConsume(
                        e.colorCanvas,
                        e.device,
                        static_cast<float>(e.colorSwap.extent.width),
                        static_cast<float>(e.colorSwap.extent.height),
                        e.colorWin.input,
                        now);
                    WindowOps colorOps{};
                    canvasTakeWindowOps(e.colorCanvas, colorOps);
                    char colorClip[96]{};
                    if (canvasTakeClip(e.colorCanvas, colorClip, 96)) {
                        SDL_SetClipboardText(colorClip);
                    }
                    if (colorEv == UiEvent::Close || colorOps.close) {
                        engineColorClose(e);
                    } else {
                        if (colorOps.openTool) {
                            engineToolOpen(e);
                        }
                        windowSetCursor(e.colorWin, canvasCursor(e.colorCanvas));
                        if (canvasWantsText(e.colorCanvas)) {
                            SDL_StartTextInput(e.colorWin.handle);
                        } else {
                            SDL_StopTextInput(e.colorWin.handle);
                        }
                        heapBind(colorFrame.cmd, e.colorHeaps);
                        canvasRecord(colorFrame.cmd, e.colorCanvas, e.colorSwap, colorFrame);
                        swapchainToPresent(e.colorSwap, colorFrame);
                        if (!swapchainSubmitPresent(e.colorSwap, e.device, colorFrame)) {
                            e.colorWin.resized = true;
                        }
                    }
                } else {
                    e.colorWin.resized = true;
                }
            }
        }
    }
    return true;
}

} // namespace burnhope
