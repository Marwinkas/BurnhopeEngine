#include "platform/Window.hpp"

#include <SDL3/SDL_vulkan.h>

namespace burnhope {
namespace {

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
    const float x = static_cast<float>(area->x) * static_cast<float>(w->pixelW) / static_cast<float>(ww);
    const float y = static_cast<float>(area->y) * static_cast<float>(w->pixelH) / static_cast<float>(wh);
    const float b = w->resizeBorderPx;
    const float W = static_cast<float>(w->pixelW);
    const float H = static_cast<float>(w->pixelH);
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

} // namespace

bool windowCreate(Window& w, const WindowConfig& config) {
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0 && !SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (!config.bordered) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    w.handle = SDL_CreateWindow(config.title, config.width, config.height, flags);
    if (w.handle == nullptr) {
        SDL_Quit();
        return false;
    }
    windowRefreshSize(w);
    SDL_SetWindowMinimumSize(w.handle, config.minWidth, config.minHeight);
    if (config.opacity < 1.0f) {
        SDL_SetWindowOpacity(w.handle, config.opacity);
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

void windowArm(Window& w) {
    w.input.pointer.pressed = 0;
    w.input.pointer.released = 0;
    w.input.wheel = 0;
    w.input.textLen = 0;
    w.input.backspace = 0;
    w.input.tab = 0;
    w.input.enter = 0;
    w.input.left = 0;
    w.input.right = 0;
    w.input.del = 0;
    w.input.ctrl = 0;
    w.input.shift = 0;
    w.input.selectAll = 0;
    w.input.copy = 0;
    w.input.cut = 0;
    w.input.pasteOn = 0;
    w.input.pasteLen = 0;
    w.input.clicks = 0;
    w.input.reload = 0;
    w.input.f2 = 0;
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
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.windowID == self) {
            w.input.wheel += e.wheel.y;
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
    case SDL_EVENT_KEY_DOWN:
        if (e.key.windowID == self || (primary && SDL_GetKeyboardFocus() == w.handle)) {
            const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
            const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            w.input.ctrl = ctrl ? 1 : 0;
            w.input.shift = shift ? 1 : 0;
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
            } else if (e.key.key == SDLK_A && ctrl) {
                w.input.selectAll = 1;
            } else if (e.key.key == SDLK_C && ctrl) {
                w.input.copy = 1;
            } else if (e.key.key == SDLK_X && ctrl) {
                w.input.cut = 1;
            } else if (e.key.key == SDLK_V && ctrl) {
                w.input.pasteOn = 1;
                char* clip = SDL_GetClipboardText();
                w.input.pasteLen = 0;
                if (clip != nullptr) {
                    for (const char* p = clip; *p != '\0' && w.input.pasteLen < 95; ++p) {
                        w.input.paste[w.input.pasteLen++] = *p;
                    }
                    w.input.paste[w.input.pasteLen] = '\0';
                    SDL_free(clip);
                }
            } else if (e.key.key == SDLK_F2 && e.key.repeat == false) {
                w.input.f2 = 1;
            } else if (e.key.key == SDLK_F5 && e.key.repeat == false) {
                w.input.reload = 1;
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
    const bool wasLeft = (w.input.pointer.down & kPointerLeft) != 0;
    const bool wasRight = (w.input.pointer.down & kPointerRight) != 0;
    if (SDL_GetMouseFocus() != w.handle) {
        if (wasLeft) {
            w.input.pointer.released |= kPointerLeft;
        }
        if (wasRight) {
            w.input.pointer.released |= kPointerRight;
        }
        w.input.pointer.down = 0;
        return;
    }
    float mx = 0.0f;
    float my = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    int ww = 0;
    int wh = 0;
    if (SDL_GetWindowSize(w.handle, &ww, &wh) && ww > 0 && wh > 0 && w.pixelW > 0 && w.pixelH > 0) {
        w.input.pointer.x = mx * static_cast<float>(w.pixelW) / static_cast<float>(ww);
        w.input.pointer.y = my * static_cast<float>(w.pixelH) / static_cast<float>(wh);
    }
    const bool left = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
    const bool right = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
    w.input.pointer.down = (left ? kPointerLeft : 0u) | (right ? kPointerRight : 0u);
    if (!wasLeft && left) {
        w.input.pointer.pressed |= kPointerLeft;
        const uint32_t now = SDL_GetTicks();
        w.input.clicks = (now - w.lastLeftMs) < 400 ? 2 : 1;
        w.lastLeftMs = now;
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
    char* clip = SDL_GetClipboardText();
    w.input.pasteLen = 0;
    if (clip != nullptr) {
        for (const char* p = clip; *p != '\0' && w.input.pasteLen < 95; ++p) {
            w.input.paste[w.input.pasteLen++] = *p;
        }
        w.input.paste[w.input.pasteLen] = '\0';
        SDL_free(clip);
    }
}

void windowPump(Window& w) {
    windowsPump(w, nullptr);
}

void windowsPump(Window& primary, Window* extra, Window* third) {
    windowArm(primary);
    if (extra != nullptr && extra->handle != nullptr) {
        windowArm(*extra);
    }
    if (third != nullptr && third->handle != nullptr) {
        windowArm(*third);
    }
    SDL_Event e{};
    while (SDL_PollEvent(&e)) {
        windowEvent(primary, e, true);
        if (extra != nullptr && extra->handle != nullptr) {
            windowEvent(*extra, e, false);
        }
        if (third != nullptr && third->handle != nullptr) {
            windowEvent(*third, e, false);
        }
    }
    windowFinish(primary);
    if (extra != nullptr && extra->handle != nullptr) {
        windowFinish(*extra);
    }
    if (third != nullptr && third->handle != nullptr) {
        windowFinish(*third);
    }
}

void windowSetCursor(Window& w, int kind) {
    if (w.handle == nullptr) {
        return;
    }
    if (kind < 0 || kind > 7) {
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
    const SDL_SystemCursor ids[8] = {
        SDL_SYSTEM_CURSOR_DEFAULT,
        SDL_SYSTEM_CURSOR_EW_RESIZE,
        SDL_SYSTEM_CURSOR_NS_RESIZE,
        SDL_SYSTEM_CURSOR_NESW_RESIZE,
        SDL_SYSTEM_CURSOR_NWSE_RESIZE,
        SDL_SYSTEM_CURSOR_TEXT,
        SDL_SYSTEM_CURSOR_POINTER,
        SDL_SYSTEM_CURSOR_CROSSHAIR,
    };
    if (w.cursors[kind] == nullptr) {
        w.cursors[kind] = SDL_CreateSystemCursor(ids[kind]);
    }
    if (w.cursors[kind] != nullptr) {
        SDL_SetCursor(w.cursors[kind]);
        w.cursorKind = kind;
    }
}

} // namespace burnhope
