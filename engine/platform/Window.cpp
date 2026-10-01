#include "platform/Window.hpp"

#include <SDL3/SDL_vulkan.h>

namespace burnhope {

bool windowCreate(Window& w, int width, int height, const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }
    w.handle = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (w.handle == nullptr) {
        SDL_Quit();
        return false;
    }
    windowRefreshSize(w);
    return true;
}

void windowDestroy(Window& w) {
    if (w.handle != nullptr) {
        SDL_DestroyWindow(w.handle);
        w.handle = nullptr;
    }
    SDL_Quit();
}

void windowRefreshSize(Window& w) {
    int px = 0;
    int py = 0;
    if (w.handle != nullptr && SDL_GetWindowSizeInPixels(w.handle, &px, &py)) {
        w.pixelW = static_cast<uint32_t>(px);
        w.pixelH = static_cast<uint32_t>(py);
    }
}

void windowPump(Window& w) {
    SDL_Event e{};
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            w.closeRequested = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (w.handle != nullptr && e.window.windowID == SDL_GetWindowID(w.handle)) {
                w.closeRequested = true;
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (w.handle != nullptr && e.window.windowID == SDL_GetWindowID(w.handle)) {
                w.resized = true;
                w.pixelW = static_cast<uint32_t>(e.window.data1);
                w.pixelH = static_cast<uint32_t>(e.window.data2);
            }
            break;
        default:
            break;
        }
    }
}

} // namespace burnhope
