#pragma once

#include <cstdint>

namespace burnhope {

constexpr uint32_t kPointerLeft = 1;
constexpr uint32_t kPointerRight = 2;
constexpr uint32_t kPointerMiddle = 4;
constexpr uint32_t kPointerX1 = 8;
constexpr uint32_t kPointerX2 = 16;
constexpr int kScanF3 = 60;

// One sample per frame. Edges are this pump only.
struct PointerFrame {
    float x = 0;
    float y = 0;
    float dx = 0;
    float dy = 0;
    uint32_t down = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
};

// Sampled once per pump. Text is capped, then cleared.
struct InputFrame {
    PointerFrame pointer{};
    float wheel = 0;
    float wheelX = 0;
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
    uint8_t up = 0;
    uint8_t down = 0;
    uint8_t home = 0;
    uint8_t end = 0;
    uint8_t pageUp = 0;
    uint8_t pageDown = 0;
    uint8_t escape = 0;
    uint8_t alt = 0;
    uint8_t super = 0;
    uint8_t caps = 0;
    uint8_t num = 0;
    uint8_t f12 = 0;
    uint8_t undo = 0;
    uint8_t redo = 0;
    uint8_t focusIn = 0;
    uint8_t focusOut = 0;
    char drop[260]{};
    uint16_t dropLen = 0;
    char editing[64]{};
    uint8_t editingLen = 0;
    uint64_t keyEdge[8]{};
    uint64_t keyDown[8]{};
    uint64_t keyUp[8]{};
};

inline bool inputKeyEdge(const InputFrame& in, int scan) {
    if (scan < 0 || scan >= 512) {
        return false;
    }
    return (in.keyEdge[static_cast<unsigned>(scan) >> 6] & (1ull << (scan & 63))) != 0;
}

inline bool inputKeyDown(const InputFrame& in, int scan) {
    if (scan < 0 || scan >= 512) {
        return false;
    }
    return (in.keyDown[static_cast<unsigned>(scan) >> 6] & (1ull << (scan & 63))) != 0;
}

} // namespace burnhope
