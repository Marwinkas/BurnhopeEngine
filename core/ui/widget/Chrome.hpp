#pragma once

#include "platform/Input.hpp"
#include "ui/Interaction.hpp"
#include "ui/Types.hpp"
#include "ui/widget/BinSlots.hpp"

#include <type_traits>

namespace burnhope {

struct UiTitleBar {};
struct UiResizable {};

struct UiChromeGesture {
    uint16_t floatWin = kUiNone;
    uint16_t resizeId = kUiNone;
    uint8_t resizeEdge = 0;
};

struct ChromePointer {
    uint8_t cursor = 0;
    uint8_t took = 0;
};

static_assert(std::is_trivially_copyable_v<UiChromeGesture>);
static_assert(std::is_trivially_copyable_v<ChromePointer>);

UiChromeGesture* uiChromeFind(UiState& s);

void uiChromeClear(UiState& s);

void uiChromePark(UiState& s, uint16_t home, const UiChromeGesture& gesture);

uint16_t uiWindowChrome(UiState& s, uint16_t parent);
// A free-floating resizable window panel (absolute, Window token) of w x h at x, y under `parent`.
uint16_t uiFloatWindow(UiState& s, uint16_t parent, float x, float y, float w, float h, bool visible = false);
// Tags a node as a window that can be resized from its edges / as a drag handle for its window.
void uiSetResizable(UiState& s, uint16_t id);
void uiSetTitleBar(UiState& s, uint16_t id);
// Title bar (drag handle) with a fold toggle at its right edge; returns the foldable body under `parent`.
uint16_t uiDragFoldHeader(UiState& s, uint16_t parent, const char* title);
// Draggable title strip: caption, spacer and a close glyph. `onClose` runs on click; without it the window
// chrome owns the glyph.
uint16_t uiTitleBar(UiState& s, uint16_t parent, const char* title, UiClickFn onClose = nullptr, void* user = nullptr, float height = 32.0f);
void uiInstallChromeBin(UiState& s);
void uiChromeSetDock(UiState& s, bool (*frame)(const UiState& s, uint16_t id), void (*move)(UiState& s, uint16_t panel, float x, float y), void (*drop)(UiState& s, uint16_t panel));
ChromePointer uiChromePointer(UiState& s, const InputFrame& in, uint16_t hit, bool press, bool down, bool release);

} // namespace burnhope
