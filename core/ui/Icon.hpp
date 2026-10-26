#pragma once

#include <cstdint>

namespace burnhope {

// Procedural sprite ids. The value is the glyph drawn by ui.slang.
enum class UiIcon : uint8_t {
    None = 0,
    Minimize = 1,
    Document = 2,
    Maximize = 2,
    Dismiss = 3,
    Folder = 4,
    Gear = 5,
    Menu = 6,
    Search = 7,
    MenuChevron = 8,
    Check = 9,
    ChevronRight = 10,
    ChevronDown = 11,
    LockOpen = 12,
    Lock = 13,
    Reset = 14,
    Crosshair = 15,
    Add = 16,
    Play = 17,
    Pause = 18,
    Stop = 19,
    Cogwheel = 20,
};

} // namespace burnhope
