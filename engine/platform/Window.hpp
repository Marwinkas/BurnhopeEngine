#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

namespace burnhope {

struct Window {
    SDL_Window* handle = nullptr;
    uint32_t pixelW = 0;
    uint32_t pixelH = 0;
    bool closeRequested = false;
    bool resized = false;
};

[[nodiscard]] bool windowCreate(Window& w, int width, int height, const char* title);
void windowDestroy(Window& w);
void windowPump(Window& w);
void windowRefreshSize(Window& w);

} // namespace burnhope
