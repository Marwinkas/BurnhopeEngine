#include "platform/Window.hpp"
#include "platform/Action.hpp"
#include "platform/PlatformUtil.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstring>
#include <cstdio>

namespace burnhope {
namespace {

bool live(const Window& w) {
    return w.handle != nullptr;
}

SDL_Window* parentOf(Window* parent) {
    return parent != nullptr ? parent->handle : nullptr;
}

void copyText(char* dst, int cap, uint8_t& len, const char* src) {
    len = 0;
    if (dst == nullptr || cap <= 0) {
        return;
    }
    const size_t n = strCopyCap(dst, static_cast<size_t>(cap), src);
    len = static_cast<uint8_t>(n > 255 ? 255 : n);
}

SDL_HitTestResult SDLCALL windowHit(SDL_Window* win, const SDL_Point* area, void* data) {
    auto* w = static_cast<Window*>(data);
    if (win == nullptr || area == nullptr || w == nullptr || w->pixelW == 0 || w->pixelH == 0) {
        return SDL_HITTEST_NORMAL;
    }
    int ww = 0;
    int wh = 0;
    if (!SDL_GetWindowSize(win, &ww, &wh) || ww <= 0 || wh <= 0) {
        return SDL_HITTEST_NORMAL;
    }
    const float x = static_cast<float>(area->x);
    const float y = static_cast<float>(area->y);
    const float b = w->resizeBorderPx;
    const float W = static_cast<float>(ww);
    const float H = static_cast<float>(wh);
    const bool left = x < b;
    const bool right = x >= W - b;
    const bool top = y < b;
    const bool bottom = y >= H - b;
    if (top && left) {
        return SDL_HITTEST_RESIZE_TOPLEFT;
    }
    if (top && right) {
        return SDL_HITTEST_RESIZE_TOPRIGHT;
    }
    if (bottom && left) {
        return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    }
    if (bottom && right) {
        return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    }
    if (top) {
        return SDL_HITTEST_RESIZE_TOP;
    }
    if (bottom) {
        return SDL_HITTEST_RESIZE_BOTTOM;
    }
    if (left) {
        return SDL_HITTEST_RESIZE_LEFT;
    }
    if (right) {
        return SDL_HITTEST_RESIZE_RIGHT;
    }
    if (y < w->titleBarPx && x < W - 120.0f) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

bool videoOn() {
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
        return true;
    }
    return SDL_Init(SDL_INIT_VIDEO);
}

} // namespace

bool windowCreate(Window& w, const WindowConfig& config) {
    if (!videoOn()) {
        return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (!config.bordered) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (config.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (config.alwaysOnTop) {
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (!config.focusable) {
        flags |= SDL_WINDOW_NOT_FOCUSABLE;
    }
    if (config.modal) {
        flags |= SDL_WINDOW_MODAL;
    }
    if (config.pixelDensity) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (config.transparent) {
        flags |= SDL_WINDOW_TRANSPARENT;
    }
    if (config.kind == 1) {
        flags |= SDL_WINDOW_UTILITY;
    } else if (config.kind == 2) {
        flags |= SDL_WINDOW_TOOLTIP;
    } else if (config.kind == 3) {
        flags |= SDL_WINDOW_POPUP_MENU;
    }
    const char* title = config.title != nullptr ? config.title : "Burnhope";
    w.handle = SDL_CreateWindow(title, config.width, config.height, flags);
    if (w.handle == nullptr) {
        return false;
    }
    windowRefreshSize(w);
    w.dpi = SDL_GetWindowDisplayScale(w.handle);
    if (w.dpi < 0.5f) {
        w.dpi = 1.0f;
    }
    w.dpiApplied = w.dpi;
    w.hidden = config.hidden;
    w.focused = true;
    if (config.minWidth > 0 && config.minHeight > 0) {
        SDL_SetWindowMinimumSize(w.handle, config.minWidth, config.minHeight);
    }
    if (config.maxWidth > 0 && config.maxHeight > 0) {
        SDL_SetWindowMaximumSize(w.handle, config.maxWidth, config.maxHeight);
    }
    if (config.x >= 0 && config.y >= 0) {
        SDL_SetWindowPosition(w.handle, config.x, config.y);
    }
    if (config.opacity < 1.0f) {
        SDL_SetWindowOpacity(w.handle, config.opacity);
    }
    if (config.aspectMin > 0.0f || config.aspectMax > 0.0f) {
        SDL_SetWindowAspectRatio(w.handle, config.aspectMin, config.aspectMax);
    }
    if (config.parent != nullptr && config.parent->handle != nullptr) {
        SDL_SetWindowParent(w.handle, config.parent->handle);
    }
    SDL_SetWindowHitTest(w.handle, windowHit, &w);
    return true;
}

bool windowCreate(Window& w, int width, int height, const char* title) {
    WindowConfig config{};
    config.title = title;
    config.width = width;
    config.height = height;
    return windowCreate(w, config);
}

void windowDestroy(Window& w) {
    for (SDL_Cursor*& cursor : w.cursors) {
        if (cursor != nullptr) {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }
    w.cursorKind = -1;
    if (w.colorCursor != nullptr) {
        SDL_DestroyCursor(w.colorCursor);
        w.colorCursor = nullptr;
    }
    if (w.handle != nullptr) {
        SDL_DestroyWindow(w.handle);
        w.handle = nullptr;
    }
}

void windowRefreshSize(Window& w) {
    int px = 0;
    int py = 0;
    if (w.handle != nullptr && SDL_GetWindowSizeInPixels(w.handle, &px, &py)) {
        w.pixelW = static_cast<uint32_t>(px);
        w.pixelH = static_cast<uint32_t>(py);
    }
}

void windowChrome(Window& w, float titleBarPx, float resizeBorderPx) {
    w.titleBarPx = titleBarPx;
    w.resizeBorderPx = resizeBorderPx;
}

bool windowTitle(Window& w, const char* title) {
    return live(w) && SDL_SetWindowTitle(w.handle, title != nullptr ? title : "");
}

bool windowPosition(Window& w, int x, int y) {
    return live(w) && SDL_SetWindowPosition(w.handle, x, y);
}

bool windowSize(Window& w, int width, int height) {
    return live(w) && SDL_SetWindowSize(w.handle, width, height);
}

bool windowMinSize(Window& w, int width, int height) {
    return live(w) && SDL_SetWindowMinimumSize(w.handle, width, height);
}

bool windowMaxSize(Window& w, int width, int height) {
    return live(w) && SDL_SetWindowMaximumSize(w.handle, width, height);
}

bool windowBordered(Window& w, bool bordered) {
    return live(w) && SDL_SetWindowBordered(w.handle, bordered);
}

bool windowResizable(Window& w, bool resizable) {
    return live(w) && SDL_SetWindowResizable(w.handle, resizable);
}

bool windowOpacity(Window& w, float opacity) {
    return live(w) && SDL_SetWindowOpacity(w.handle, opacity);
}

bool windowAlwaysOnTop(Window& w, bool onTop) {
    return live(w) && SDL_SetWindowAlwaysOnTop(w.handle, onTop);
}

bool windowFullscreen(Window& w, bool fullscreen) {
    return live(w) && SDL_SetWindowFullscreen(w.handle, fullscreen);
}

bool windowMaximize(Window& w) {
    return live(w) && SDL_MaximizeWindow(w.handle);
}

bool windowMinimize(Window& w) {
    return live(w) && SDL_MinimizeWindow(w.handle);
}

bool windowRestore(Window& w) {
    return live(w) && SDL_RestoreWindow(w.handle);
}

bool windowToggleMaximize(Window& w) {
    if (!live(w)) {
        return false;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(w.handle);
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
        return SDL_RestoreWindow(w.handle);
    }
    return SDL_MaximizeWindow(w.handle);
}

bool windowShow(Window& w) {
    return live(w) && SDL_ShowWindow(w.handle);
}

bool windowHide(Window& w) {
    return live(w) && SDL_HideWindow(w.handle);
}

bool windowRaise(Window& w) {
    return live(w) && SDL_RaiseWindow(w.handle);
}

bool windowParent(Window& w, Window* parent) {
    return live(w) && SDL_SetWindowParent(w.handle, parentOf(parent));
}

bool windowModal(Window& w, bool modal) {
    return live(w) && SDL_SetWindowModal(w.handle, modal);
}

bool windowFocusable(Window& w, bool focusable) {
    return live(w) && SDL_SetWindowFocusable(w.handle, focusable);
}

bool windowGrab(Window& w, bool keyboard, bool mouse) {
    if (!live(w)) {
        return false;
    }
    const bool keys = SDL_SetWindowKeyboardGrab(w.handle, keyboard);
    const bool pointer = SDL_SetWindowMouseGrab(w.handle, mouse);
    return keys && pointer;
}

bool windowRelativeMouse(Window& w, bool on) {
    if (!live(w)) {
        return false;
    }
    if (on && !w.relOn) {
        SDL_GetMouseState(&w.relX, &w.relY);
        w.relEat = true;
    }
    const bool ok = SDL_SetWindowRelativeMouseMode(w.handle, on);
    if (ok && !on && w.relOn) {
        SDL_WarpMouseInWindow(w.handle, w.relX, w.relY);
    }
    if (ok) {
        w.relOn = on;
    }
    return ok;
}

bool windowAspect(Window& w, float minAspect, float maxAspect) {
    return live(w) && SDL_SetWindowAspectRatio(w.handle, minAspect, maxAspect);
}

bool windowFlash(Window& w, int op) {
    if (!live(w)) {
        return false;
    }
    SDL_FlashOperation flash = SDL_FLASH_CANCEL;
    if (op == 1) {
        flash = SDL_FLASH_BRIEFLY;
    } else if (op > 1) {
        flash = SDL_FLASH_UNTIL_FOCUSED;
    }
    return SDL_FlashWindow(w.handle, flash);
}

bool windowProgress(Window& w, int state, float value) {
    if (!live(w)) {
        return false;
    }
    SDL_ProgressState progress = SDL_PROGRESS_STATE_NONE;
    if (state == 1) {
        progress = SDL_PROGRESS_STATE_INDETERMINATE;
    } else if (state == 2) {
        progress = SDL_PROGRESS_STATE_NORMAL;
    } else if (state == 3) {
        progress = SDL_PROGRESS_STATE_PAUSED;
    } else if (state == 4) {
        progress = SDL_PROGRESS_STATE_ERROR;
    }
    const bool ok = SDL_SetWindowProgressState(w.handle, progress);
    if (state == 2) {
        return ok && SDL_SetWindowProgressValue(w.handle, value);
    }
    return ok;
}

bool windowFillDocument(Window& w, bool fill) {
    return live(w) && SDL_SetWindowFillDocument(w.handle, fill);
}

bool windowSync(Window& w) {
    return live(w) && SDL_SyncWindow(w.handle);
}

bool windowSystemMenu(Window& w, int x, int y) {
    return live(w) && SDL_ShowWindowSystemMenu(w.handle, x, y);
}

bool windowMouseRect(Window& w, int x, int y, int width, int height) {
    if (!live(w)) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return SDL_SetWindowMouseRect(w.handle, nullptr);
    }
    const SDL_Rect rect{x, y, width, height};
    return SDL_SetWindowMouseRect(w.handle, &rect);
}

bool windowIcon(Window& w, int width, int height, const uint8_t* rgba) {
    if (!live(w) || width <= 0 || height <= 0 || rgba == nullptr) {
        return false;
    }
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr || surface->pixels == nullptr) {
        return false;
    }
    std::memcpy(surface->pixels, rgba, static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    const bool ok = SDL_SetWindowIcon(w.handle, surface);
    SDL_DestroySurface(surface);
    return ok;
}

bool windowShape(Window& w, int width, int height, const uint8_t* rgba) {
    if (!live(w) || width <= 0 || height <= 0 || rgba == nullptr) {
        return false;
    }
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr || surface->pixels == nullptr) {
        return false;
    }
    std::memcpy(surface->pixels, rgba, static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    const bool ok = SDL_SetWindowShape(w.handle, surface);
    SDL_DestroySurface(surface);
    return ok;
}

void windowApply(Window& w, const WindowCommand& command) {
    if (!live(w)) {
        return;
    }
    if (command.minimize) {
        SDL_MinimizeWindow(w.handle);
    }
    if (command.toggleMaximize) {
        (void)windowToggleMaximize(w);
    }
    if (command.raise) {
        SDL_RaiseWindow(w.handle);
    }
    if (command.alwaysOnTop >= 0) {
        SDL_SetWindowAlwaysOnTop(w.handle, command.alwaysOnTop != 0);
    }
    if (command.bordered >= 0) {
        SDL_SetWindowBordered(w.handle, command.bordered != 0);
    }
    if (command.resizable >= 0) {
        SDL_SetWindowResizable(w.handle, command.resizable != 0);
    }
    if (command.fullscreen >= 0) {
        SDL_SetWindowFullscreen(w.handle, command.fullscreen != 0);
    }
    if (command.opacity >= 0.0f) {
        SDL_SetWindowOpacity(w.handle, command.opacity);
    }
    if (command.flash != 0) {
        (void)windowFlash(w, command.flash);
    }
}

bool windowQuery(const Window& w, WindowState& out) {
    out = {};
    if (!live(w)) {
        return false;
    }
    SDL_GetWindowPosition(w.handle, &out.x, &out.y);
    SDL_GetWindowSize(w.handle, &out.width, &out.height);
    out.pixelW = w.pixelW;
    out.pixelH = w.pixelH;
    out.opacity = SDL_GetWindowOpacity(w.handle);
    out.pixelDensity = SDL_GetWindowPixelDensity(w.handle);
    out.displayScale = SDL_GetWindowDisplayScale(w.handle);
    SDL_Rect safe{};
    if (SDL_GetWindowSafeArea(w.handle, &safe)) {
        out.safeX = safe.x;
        out.safeY = safe.y;
        out.safeW = safe.w;
        out.safeH = safe.h;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(w.handle);
    out.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    out.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
    out.fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    out.hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
    out.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    out.mouseFocus = (flags & SDL_WINDOW_MOUSE_FOCUS) != 0;
    out.alwaysOnTop = (flags & SDL_WINDOW_ALWAYS_ON_TOP) != 0;
    out.bordered = (flags & SDL_WINDOW_BORDERLESS) == 0;
    out.resizable = (flags & SDL_WINDOW_RESIZABLE) != 0;
    return true;
}

bool windowDisplay(const Window& w, DisplayInfo& out) {
    out = {};
    if (!live(w)) {
        return false;
    }
    const SDL_DisplayID id = SDL_GetDisplayForWindow(w.handle);
    SDL_Rect bounds{};
    SDL_Rect usable{};
    if (SDL_GetDisplayBounds(id, &bounds)) {
        out.x = bounds.x;
        out.y = bounds.y;
        out.width = bounds.w;
        out.height = bounds.h;
    }
    if (SDL_GetDisplayUsableBounds(id, &usable)) {
        out.usableX = usable.x;
        out.usableY = usable.y;
        out.usableW = usable.w;
        out.usableH = usable.h;
    }
    out.scale = SDL_GetDisplayContentScale(id);
    out.theme = platformTheme();
    return true;
}

void windowText(Window& w, bool on) {
    if (!live(w)) {
        return;
    }
    if (on) {
        SDL_StartTextInput(w.handle);
    } else {
        SDL_StopTextInput(w.handle);
    }
}

void windowTextArea(Window& w, int x, int y, int width, int height) {
    if (!live(w)) {
        return;
    }
    SDL_Rect area{x, y, width, height};
    SDL_SetTextInputArea(w.handle, &area, 0);
}

void platformSleep(int ms) {
    if (ms > 0) {
        SDL_Delay(static_cast<Uint32>(ms));
    }
}

void platformPace(int hz, uint64_t& nextTick) {
    if (hz <= 0) {
        return;
    }
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t step = freq / static_cast<uint64_t>(hz);
    if (step == 0) {
        return;
    }
    if (nextTick == 0 || nextTick + step * 4 < now) {
        nextTick = now + step;
        return;
    }
    if (now < nextTick) {
        const double ms = 1000.0 * static_cast<double>(nextTick - now) / static_cast<double>(freq);
        if (ms > 2.0) {
            SDL_Delay(static_cast<Uint32>(ms - 1.5));
        }
        while (SDL_GetPerformanceCounter() < nextTick) {
        }
    }
    nextTick += step;
}

void windowClipboardSet(const char* text) {
    SDL_SetClipboardText(text != nullptr ? text : "");
}

bool windowClipboardGet(char* out, int cap) {
    char* clip = SDL_GetClipboardText();
    uint8_t len = 0;
    copyText(out, cap, len, clip);
    if (clip != nullptr) {
        SDL_free(clip);
    }
    return clip != nullptr;
}

const char* platformError() {
    const char* err = SDL_GetError();
    return err != nullptr ? err : "";
}

int platformTheme() {
    const SDL_SystemTheme theme = SDL_GetSystemTheme();
    if (theme == SDL_SYSTEM_THEME_LIGHT) {
        return 1;
    }
    if (theme == SDL_SYSTEM_THEME_DARK) {
        return 2;
    }
    return 0;
}

void platformScreensaver(bool enabled) {
    if (enabled) {
        SDL_EnableScreenSaver();
    } else {
        SDL_DisableScreenSaver();
    }
}

void platformShutdown() {
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
        SDL_Quit();
    }
}

void windowArm(Window& w) {
    const uint32_t down = w.input.pointer.down;
    const float x = w.input.pointer.x;
    const float y = w.input.pointer.y;
    w.input = {};
    w.input.pointer.down = down;
    w.input.pointer.x = x;
    w.input.pointer.y = y;
    w.motionDx = 0.0f;
    w.motionDy = 0.0f;
}

void windowEvent(Window& w, const SDL_Event& e, bool primary) {
    const SDL_WindowID self = w.handle != nullptr ? SDL_GetWindowID(w.handle) : 0;
    switch (e.type) {
    case SDL_EVENT_QUIT:
        if (primary) {
            w.closeRequested = true;
        }
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (w.handle != nullptr && e.window.windowID == self) {
            w.closeRequested = true;
        }
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (w.handle != nullptr && e.window.windowID == self) {
            w.resized = true;
            w.pixelW = static_cast<uint32_t>(e.window.data1);
            w.pixelH = static_cast<uint32_t>(e.window.data2);
        }
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (w.handle != nullptr && e.window.windowID == self) {
            w.dpi = SDL_GetWindowDisplayScale(w.handle);
            if (w.dpi < 0.5f) {
                w.dpi = 1.0f;
            }
            w.dpiChanged = true;
        }
        break;
    case SDL_EVENT_WINDOW_MOVED:
        if (w.handle != nullptr && e.window.windowID == self) {
            w.moved = true;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (w.handle != nullptr && e.window.windowID == self) {
            w.input.focusIn = 1;
            w.focused = true;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (w.handle != nullptr && e.window.windowID == self) {
            w.input.focusOut = 1;
            w.focused = false;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.windowID == self) {
            w.motionDx += e.motion.xrel;
            w.motionDy += e.motion.yrel;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.windowID == self) {
            w.input.wheel += e.wheel.y;
            w.input.wheelX += e.wheel.x;
        }
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.windowID == self && e.drop.data != nullptr) {
            uint8_t n = 0;
            copyText(w.input.drop, static_cast<int>(sizeof(w.input.drop)), n, e.drop.data);
            w.input.dropLen = n;
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (e.text.windowID == self && e.text.text != nullptr) {
            for (const char* p = e.text.text; *p != '\0' && w.input.textLen < 63; ++p) {
                const unsigned char ch = static_cast<unsigned char>(*p);
                if (ch >= 32) {
                    w.input.text[w.input.textLen++] = static_cast<char>(ch);
                }
            }
        }
        break;
    case SDL_EVENT_TEXT_EDITING:
        if (e.edit.windowID == self && e.edit.text != nullptr) {
            w.input.editingLen = 0;
            for (const char* p = e.edit.text; *p != '\0' && w.input.editingLen < 63; ++p) {
                w.input.editing[w.input.editingLen++] = *p;
            }
            w.input.editing[w.input.editingLen] = '\0';
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (e.key.windowID == self || (primary && SDL_GetKeyboardFocus() == w.handle)) {
            const int scan = static_cast<int>(e.key.scancode);
            if (scan >= 0 && scan < 512) {
                w.input.keyUp[scan >> 6] |= 1ull << (scan & 63);
                w.holdMs[scan] = 0;
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.windowID == self || (primary && SDL_GetKeyboardFocus() == w.handle)) {
            const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
            const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            const bool alt = (e.key.mod & SDL_KMOD_ALT) != 0;
            w.input.ctrl = ctrl ? 1 : 0;
            w.input.shift = shift ? 1 : 0;
            w.input.alt = alt ? 1 : 0;
            if (e.key.key == SDLK_BACKSPACE) {
                w.input.backspace = 1;
            } else if (e.key.key == SDLK_DELETE) {
                w.input.del = 1;
            } else if (e.key.key == SDLK_TAB) {
                w.input.tab = 1;
            } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                w.input.enter = 1;
            } else if (e.key.key == SDLK_LEFT) {
                w.input.left = 1;
            } else if (e.key.key == SDLK_RIGHT) {
                w.input.right = 1;
            } else if (e.key.key == SDLK_UP) {
                w.input.up = 1;
            } else if (e.key.key == SDLK_DOWN) {
                w.input.down = 1;
            } else if (e.key.key == SDLK_HOME) {
                w.input.home = 1;
            } else if (e.key.key == SDLK_END) {
                w.input.end = 1;
            } else if (e.key.key == SDLK_PAGEUP) {
                w.input.pageUp = 1;
            } else if (e.key.key == SDLK_PAGEDOWN) {
                w.input.pageDown = 1;
            } else if (e.key.key == SDLK_ESCAPE) {
                w.input.escape = 1;
            } else if (e.key.key == SDLK_A && ctrl) {
                w.input.selectAll = 1;
            } else if (e.key.key == SDLK_C && ctrl) {
                w.input.copy = 1;
            } else if (e.key.key == SDLK_X && ctrl) {
                w.input.cut = 1;
            } else if (e.key.key == SDLK_V && ctrl) {
                w.input.pasteOn = 1;
                char* clip = SDL_GetClipboardText();
                copyText(w.input.paste, static_cast<int>(sizeof(w.input.paste)), w.input.pasteLen, clip);
                if (clip != nullptr) {
                    SDL_free(clip);
                }
            } else if (e.key.key == SDLK_Z && ctrl) {
                if (shift) {
                    w.input.redo = 1;
                } else {
                    w.input.undo = 1;
                }
            } else if (e.key.key == SDLK_Y && ctrl) {
                w.input.redo = 1;
            } else if (e.key.key == SDLK_F2 && e.key.repeat == false) {
                w.input.f2 = 1;
            } else if (e.key.key == SDLK_F5 && e.key.repeat == false) {
                w.input.reload = 1;
            } else if (e.key.key == SDLK_F12 && e.key.repeat == false) {
                w.input.f12 = 1;
            }
            const int scan = static_cast<int>(e.key.scancode);
            if (scan >= 0 && scan < 512) {
                w.input.keyEdge[scan >> 6] |= 1ull << (scan & 63);
                const uint8_t mods = static_cast<uint8_t>((w.input.ctrl ? 1 : 0) | (w.input.shift ? 2 : 0) | (w.input.alt ? 4 : 0) | ((e.key.mod & SDL_KMOD_GUI) != 0 ? 8 : 0));
                actionOfferKey(scan, mods);
            }
        }
        break;
    default:
        break;
    }
}

void windowFinish(Window& w) {
    if (w.handle == nullptr) {
        return;
    }
    const SDL_Keymod mods = SDL_GetModState();
    w.input.ctrl = (mods & SDL_KMOD_CTRL) != 0 ? 1 : 0;
    w.input.shift = (mods & SDL_KMOD_SHIFT) != 0 ? 1 : 0;
    w.input.alt = (mods & SDL_KMOD_ALT) != 0 ? 1 : 0;
    w.input.super = (mods & SDL_KMOD_GUI) != 0 ? 1 : 0;
    w.input.caps = (mods & SDL_KMOD_CAPS) != 0 ? 1 : 0;
    w.input.num = (mods & SDL_KMOD_NUM) != 0 ? 1 : 0;
    int keyCount = 0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);
    if (keys != nullptr) {
        const int n = keyCount < 512 ? keyCount : 512;
        for (int i = 0; i < n; ++i) {
            if (keys[i]) {
                w.input.keyDown[i >> 6] |= 1ull << (i & 63);
                if (w.holdMs[i] < 60000) {
                    w.holdMs[i] = static_cast<uint16_t>(w.holdMs[i] + 16);
                }
            }
        }
    }
    const bool wasLeft = (w.input.pointer.down & kPointerLeft) != 0;
    const bool wasRight = (w.input.pointer.down & kPointerRight) != 0;
    const bool wasMiddle = (w.input.pointer.down & kPointerMiddle) != 0;
    if (SDL_GetMouseFocus() != w.handle && !w.relOn) {
        if (wasLeft) {
            w.input.pointer.released |= kPointerLeft;
        }
        if (wasRight) {
            w.input.pointer.released |= kPointerRight;
        }
        if (wasMiddle) {
            w.input.pointer.released |= kPointerMiddle;
        }
        w.input.pointer.down = 0;
        return;
    }
    if (SDL_GetMouseFocus() != w.handle) {
        w.input.pointer.dx = w.motionDx;
        w.input.pointer.dy = w.motionDy;
        if (w.relEat) {
            w.input.pointer.dx = 0.0f;
            w.input.pointer.dy = 0.0f;
            w.relEat = false;
        }
        return;
    }
    float mx = 0.0f;
    float my = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    int ww = 0;
    int wh = 0;
    if (SDL_GetWindowSize(w.handle, &ww, &wh) && ww > 0 && wh > 0 && w.pixelW > 0 && w.pixelH > 0) {
        const float prevX = w.input.pointer.x;
        const float prevY = w.input.pointer.y;
        w.input.pointer.x = mx * static_cast<float>(w.pixelW) / static_cast<float>(ww);
        w.input.pointer.y = my * static_cast<float>(w.pixelH) / static_cast<float>(wh);
        if (w.relOn) {
            w.input.pointer.dx = w.motionDx;
            w.input.pointer.dy = w.motionDy;
            if (w.relEat) {
                w.input.pointer.dx = 0.0f;
                w.input.pointer.dy = 0.0f;
                w.relEat = false;
            }
        } else {
            w.input.pointer.dx = w.input.pointer.x - prevX;
            w.input.pointer.dy = w.input.pointer.y - prevY;
        }
        if (w.smooth) {
            w.smoothDx = w.smoothDx * 0.5f + w.input.pointer.dx * 0.5f;
            w.smoothDy = w.smoothDy * 0.5f + w.input.pointer.dy * 0.5f;
            w.input.pointer.dx = w.smoothDx;
            w.input.pointer.dy = w.smoothDy;
        }
    }
    const bool left = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
    const bool right = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
    const bool middle = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) != 0;
    const bool x1 = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_X1)) != 0;
    const bool x2 = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_X2)) != 0;
    const bool wasX1 = (w.input.pointer.down & kPointerX1) != 0;
    const bool wasX2 = (w.input.pointer.down & kPointerX2) != 0;
    w.input.pointer.down = (left ? kPointerLeft : 0u) | (right ? kPointerRight : 0u) | (middle ? kPointerMiddle : 0u)
        | (x1 ? kPointerX1 : 0u) | (x2 ? kPointerX2 : 0u);
    if (!wasX1 && x1) {
        w.input.pointer.pressed |= kPointerX1;
    }
    if (wasX1 && !x1) {
        w.input.pointer.released |= kPointerX1;
    }
    if (!wasX2 && x2) {
        w.input.pointer.pressed |= kPointerX2;
    }
    if (wasX2 && !x2) {
        w.input.pointer.released |= kPointerX2;
    }
    if (!wasLeft && left) {
        w.input.pointer.pressed |= kPointerLeft;
        const uint32_t now = SDL_GetTicks();
        w.input.clicks = 1;
        const float movedX = w.input.pointer.x - w.lastClickX;
        const float movedY = w.input.pointer.y - w.lastClickY;
        if ((now - w.lastLeftMs) < 400 && movedX * movedX + movedY * movedY <= 16.0f) {
            w.input.clicks = w.lastClickN >= 2 ? 3 : 2;
        }
        w.lastClickN = w.input.clicks;
        w.lastLeftMs = now;
        w.lastClickX = w.input.pointer.x;
        w.lastClickY = w.input.pointer.y;
    }
    if (wasLeft && !left) {
        w.input.pointer.released |= kPointerLeft;
    }
    if (!wasRight && right) {
        w.input.pointer.pressed |= kPointerRight;
    }
    if (wasRight && !right) {
        w.input.pointer.released |= kPointerRight;
    }
    if (!wasMiddle && middle) {
        w.input.pointer.pressed |= kPointerMiddle;
    }
    if (wasMiddle && !middle) {
        w.input.pointer.released |= kPointerMiddle;
    }
}

void windowsPump(Window** list, int count) {
    if (list == nullptr || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (list[i] != nullptr && list[i]->handle != nullptr) {
            windowArm(*list[i]);
        }
    }
    SDL_Event e{};
    while (SDL_PollEvent(&e)) {
        for (int i = 0; i < count; ++i) {
            if (list[i] != nullptr && list[i]->handle != nullptr) {
                windowEvent(*list[i], e, i == 0);
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        if (list[i] != nullptr && list[i]->handle != nullptr) {
            windowFinish(*list[i]);
        }
    }
}

void windowsPump(Window& primary, Window* extra, Window* third) {
    Window* list[3] = {&primary, extra, third};
    int count = 1;
    if (extra != nullptr && extra->handle != nullptr) {
        list[count++] = extra;
    }
    if (third != nullptr && third->handle != nullptr) {
        list[count++] = third;
    }
    windowsPump(list, count);
}

void windowPump(Window& w) {
    windowsPump(w, nullptr, nullptr);
}

void windowSetCursor(Window& w, int kind) {
    if (w.handle == nullptr) {
        return;
    }
    if (kind < 0 || kind > 11) {
        kind = 0;
    }
    const float x = w.input.pointer.x;
    const float y = w.input.pointer.y;
    const float b = w.resizeBorderPx;
    const bool border = w.pixelW > 0 && w.pixelH > 0
        && (x < b || y < b || x >= static_cast<float>(w.pixelW) - b || y >= static_cast<float>(w.pixelH) - b);
    if (border) {
        return;
    }
    if (w.cursorKind == kind && w.cursors[kind] != nullptr) {
        return;
    }
    const SDL_SystemCursor ids[12] = {
        SDL_SYSTEM_CURSOR_DEFAULT,
        SDL_SYSTEM_CURSOR_EW_RESIZE,
        SDL_SYSTEM_CURSOR_NS_RESIZE,
        SDL_SYSTEM_CURSOR_NESW_RESIZE,
        SDL_SYSTEM_CURSOR_NWSE_RESIZE,
        SDL_SYSTEM_CURSOR_TEXT,
        SDL_SYSTEM_CURSOR_POINTER,
        SDL_SYSTEM_CURSOR_CROSSHAIR,
        SDL_SYSTEM_CURSOR_MOVE,
        SDL_SYSTEM_CURSOR_NOT_ALLOWED,
        SDL_SYSTEM_CURSOR_WAIT,
        SDL_SYSTEM_CURSOR_PROGRESS,
    };
    if (w.cursors[kind] == nullptr) {
        w.cursors[kind] = SDL_CreateSystemCursor(ids[kind]);
    }
    if (w.cursors[kind] != nullptr) {
        SDL_SetCursor(w.cursors[kind]);
        w.cursorKind = kind;
    }
}

uint16_t windowHoldMs(const Window& w, int scancode) {
    if (scancode < 0 || scancode >= 512) {
        return 0;
    }
    return w.holdMs[scancode];
}

bool windowFullscreenKind(Window& w, int kind) {
    if (!live(w)) {
        return false;
    }
    if (kind <= 0) {
        return SDL_SetWindowFullscreen(w.handle, false);
    }
    if (kind == 1) {
        SDL_SetWindowFullscreenMode(w.handle, nullptr);
        return SDL_SetWindowFullscreen(w.handle, true);
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(w.handle);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
    if (mode == nullptr) {
        return false;
    }
    SDL_SetWindowFullscreenMode(w.handle, mode);
    return SDL_SetWindowFullscreen(w.handle, true);
}

bool windowCenter(Window& w) {
    if (!live(w)) {
        return false;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(w.handle);
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(display, &bounds)) {
        return false;
    }
    int ww = 0;
    int wh = 0;
    SDL_GetWindowSize(w.handle, &ww, &wh);
    return SDL_SetWindowPosition(w.handle, bounds.x + (bounds.w - ww) / 2, bounds.y + (bounds.h - wh) / 2);
}

bool windowToDisplay(Window& w, int displayIndex) {
    if (!live(w)) {
        return false;
    }
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids == nullptr || displayIndex < 0 || displayIndex >= count) {
        if (ids != nullptr) {
            SDL_free(ids);
        }
        return false;
    }
    SDL_Rect bounds{};
    const bool ok = SDL_GetDisplayBounds(ids[displayIndex], &bounds);
    SDL_free(ids);
    if (!ok) {
        return false;
    }
    return SDL_SetWindowPosition(w.handle, bounds.x + 40, bounds.y + 40);
}

bool windowClickThrough(Window& w, bool on) {
    if (!live(w)) {
        return false;
    }
    if (!on) {
        return SDL_SetWindowShape(w.handle, nullptr);
    }
    SDL_Surface* shape = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    if (shape == nullptr) {
        return false;
    }
    SDL_ClearSurface(shape, 0.0f, 0.0f, 0.0f, 0.0f);
    const bool ok = SDL_SetWindowShape(w.handle, shape);
    SDL_DestroySurface(shape);
    return ok;
}

bool windowMouseSmooth(Window& w, bool on) {
    w.smooth = on;
    if (!on) {
        w.smoothDx = 0.0f;
        w.smoothDy = 0.0f;
    }
    return true;
}

bool windowCursorRgba(Window& w, int width, int height, const uint8_t* rgba, int hotX, int hotY) {
    if (!live(w) || rgba == nullptr || width < 1 || height < 1) {
        return false;
    }
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        return false;
    }
    if (!SDL_LockSurface(surface)) {
        SDL_DestroySurface(surface);
        return false;
    }
    std::memcpy(surface->pixels, rgba, static_cast<size_t>(width * height * 4));
    SDL_UnlockSurface(surface);
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hotX, hotY);
    SDL_DestroySurface(surface);
    if (cursor == nullptr) {
        return false;
    }
    if (w.colorCursor != nullptr) {
        SDL_DestroyCursor(w.colorCursor);
    }
    w.colorCursor = cursor;
    SDL_SetCursor(cursor);
    w.cursorKind = -2;
    return true;
}

void placePath(char* out, int cap, const char* name) {
    char safe[32]{};
    int n = 0;
    if (name != nullptr) {
        for (const char* p = name; *p != '\0' && n < 31; ++p) {
            const char ch = *p;
            safe[n++] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) ? ch : '_';
        }
    }
    safe[n] = '\0';
    char file[48]{};
    std::snprintf(file, sizeof(file), "burnhope-win-%s.txt", safe[0] != '\0' ? safe : "window");
    if (!platformCachePath(out, static_cast<size_t>(cap), file)) {
        out[0] = '\0';
    }
}

bool windowCreateSurface(Window& w, VkInstance instance, VkSurfaceKHR& out) {
    if (!live(w) || instance == VK_NULL_HANDLE) {
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(w.handle, instance, nullptr, &out)) {
        return false;
    }
    return true;
}

void windowRemember(const Window& w, const char* name) {
    if (w.handle == nullptr) {
        return;
    }
    int x = 0;
    int y = 0;
    int ww = 0;
    int wh = 0;
    SDL_GetWindowPosition(w.handle, &x, &y);
    SDL_GetWindowSize(w.handle, &ww, &wh);
    const int maxed = SDL_GetWindowFlags(w.handle) & SDL_WINDOW_MAXIMIZED ? 1 : 0;
    char path[256]{};
    placePath(path, 256, name);
    SDL_IOStream* file = SDL_IOFromFile(path, "w");
    if (file == nullptr) {
        return;
    }
    char line[64]{};
    std::snprintf(line, sizeof(line), "%d %d %d %d %d\n", x, y, ww, wh, maxed);
    SDL_WriteIO(file, line, std::strlen(line));
    SDL_CloseIO(file);
}

void windowRecall(WindowConfig& config, const char* name) {
    char path[256]{};
    placePath(path, 256, name);
    SDL_IOStream* file = SDL_IOFromFile(path, "r");
    if (file == nullptr) {
        return;
    }
    char line[64]{};
    SDL_ReadIO(file, line, sizeof(line) - 1);
    SDL_CloseIO(file);
    int x = 0;
    int y = 0;
    int ww = 0;
    int wh = 0;
    int maxed = 0;
    if (std::sscanf(line, "%d %d %d %d %d", &x, &y, &ww, &wh, &maxed) != 5) {
        return;
    }
    if (ww > 0 && wh > 0) {
        config.x = x;
        config.y = y;
        config.width = ww;
        config.height = wh;
        config.maximized = maxed != 0;
    }
}

} // namespace burnhope
