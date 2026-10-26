#pragma once

#include <cstdint>
#include <string_view>

namespace burnhope {

// Built-in sprite ids. The value is the cell of the icon atlas ui.slang samples (kUiIconAtlasCells wide).
constexpr uint32_t kUiIconAtlasCells = 8;
constexpr uint32_t kUiIconCell = 32;
constexpr uint32_t kUiIconAtlasEdge = kUiIconAtlasCells * kUiIconCell;

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
    ChevronLeft = 21,
};

// Maps a human name ("plus", "gear", "chevron_left") to its sprite; unknown names give UiIcon::None.
UiIcon uiIconFromName(std::string_view name);

// The vector asset of a procedural sprite ("assets/icons/chevron_down.svg"); nullptr for UiIcon::None and for glyphs without one.
[[nodiscard]] const char* uiIconAssetPath(UiIcon icon);

// True for an asset-path icon ("assets/icons/Snap.svg"): not a procedural name, and it holds a '/' or ends in .svg / .png.
[[nodiscard]] bool uiIconIsPath(std::string_view name);

} // namespace burnhope
