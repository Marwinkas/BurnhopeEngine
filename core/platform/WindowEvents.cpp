#include "platform/Window.hpp"
#include "platform/Action.hpp"
#include "platform/PlatformUtil.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <cstdio>

namespace burnhope {
namespace {

void copyText(char* dst, int cap, uint8_t& len, const char* src) {
    len = 0;
    if (dst == nullptr || cap <= 0) {
        return;
    }
    const size_t n = strCopyCap(dst, static_cast<size_t>(cap), src);
    len = static_cast<uint8_t>(n > 255 ? 255 : n);
}

} // namespace

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
        if (w.relOn) {
            w.input.pointer.x = w.relX * static_cast<float>(w.pixelW) / static_cast<float>(ww);
            w.input.pointer.y = w.relY * static_cast<float>(w.pixelH) / static_cast<float>(wh);
            w.input.pointer.dx = w.motionDx;
            w.input.pointer.dy = w.motionDy;
            if (w.relEat) {
                w.input.pointer.dx = 0.0f;
                w.input.pointer.dy = 0.0f;
                w.relEat = false;
            }
        } else {
            w.input.pointer.x = mx * static_cast<float>(w.pixelW) / static_cast<float>(ww);
            w.input.pointer.y = my * static_cast<float>(w.pixelH) / static_cast<float>(wh);
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

uint16_t windowHoldMs(const Window& w, int scancode) {
    if (scancode < 0 || scancode >= 512) {
        return 0;
    }
    return w.holdMs[scancode];
}

} // namespace burnhope
