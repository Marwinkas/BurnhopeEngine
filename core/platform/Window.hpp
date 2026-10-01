#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

namespace burnhope {

constexpr uint32_t kPointerLeft = 1;
constexpr uint32_t kPointerRight = 2;

// One sample per frame. Edges are this pump only.
struct PointerFrame {
    float x = 0;
    float y = 0;
    uint32_t down = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
};

// Sampled once per pump. Text is ASCII only, capped, then cleared.
struct InputFrame {
    PointerFrame pointer{};
    float wheel = 0;
    char text[64]{};
    uint8_t textLen = 0;
    char paste[96]{};
    uint8_t pasteLen = 0;
    uint8_t backspace = 0;
    uint8_t tab = 0;
    uint8_t enter = 0;
    uint8_t left = 0;
    uint8_t right = 0;
    uint8_t del = 0;
    uint8_t ctrl = 0;
    uint8_t shift = 0;
    uint8_t selectAll = 0;
    uint8_t copy = 0;
    uint8_t cut = 0;
    uint8_t pasteOn = 0;
    uint8_t clicks = 0;
    uint8_t reload = 0;
    uint8_t f2 = 0;
};

struct WindowConfig {
    const char* title = "Burnhope";
    int width = 1600;
    int height = 900;
    int minWidth = 720;
    int minHeight = 480;
    bool bordered = false;
    bool resizable = true;
    float opacity = 1.0f;
};

struct Window {
    SDL_Window* handle = nullptr;
    uint32_t pixelW = 0;
    uint32_t pixelH = 0;
    bool closeRequested = false;
    bool resized = false;
    float titleBarPx = 40.0f;
    float resizeBorderPx = 6.0f;
    uint32_t lastLeftMs = 0;
    int cursorKind = -1;
    SDL_Cursor* cursors[8]{};
    InputFrame input{};
};

[[nodiscard]] bool windowCreate(Window& w, const WindowConfig& config);
[[nodiscard]] bool windowCreate(Window& w, int width, int height, const char* title);
void windowDestroy(Window& w);
void windowPump(Window& w);
void windowsPump(Window& primary, Window* extra = nullptr, Window* third = nullptr);
void windowSetCursor(Window& w, int kind);
void windowRefreshSize(Window& w);

} // namespace burnhope
