#pragma once

#include "gui/GuiHost.hpp"

namespace burnhope::gui {

// Colour picker popup state (the "colorPicker" overlay of assets/gui/menus.json). One per GuiHost; it works through the
// public guiHost* calls only, so it never touches the host's tables.
struct GuiColorPicker {
    struct Channel {
        GuiColorPicker* owner = nullptr;
        uint32_t index = 0;
    };
    GuiHost* host = nullptr;
    GuiColorFn fn = nullptr;
    void* user = nullptr;
    float rgb[3] = {1.0f, 1.0f, 1.0f};
    Channel channel[3] = {};
    bool bound = false;
};

// Binds the popup's controls on first use, loads `rgb` into them and opens the popup below `anchor`.
[[nodiscard]] bool guiColorPickerOpen(GuiHost* host, GuiColorPicker& picker, const char* anchor, const float (&rgb)[3], GuiColorFn fn,
                                      void* user);

} // namespace burnhope::gui
