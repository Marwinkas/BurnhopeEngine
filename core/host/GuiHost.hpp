#pragma once

#include <cstdint>

#include "rhi/ui/Canvas.hpp"

namespace burnhope {

// Owns one bh_gui instance (flecs world, style sheet, context) behind an opaque handle, so Layer 3 never sees flecs or Yoga.
// The GUI runs inside Canvas on a worker thread; widget callbacks are queued there and fired by guiHostPump on the main
// thread, so application code never runs concurrently with the draw phase.
struct GuiHost;

using GuiClickFn = void (*)(void* user, uint32_t action);
using GuiValueFn = void (*)(void* user, float value);
using GuiTextFn = void (*)(void* user, const char* text);

// Loads the style sheet and the screen layout and installs the GUI as the primitive layer of `canvas`.
// Returns nullptr when either file fails to load; nothing is attached then.
[[nodiscard]] GuiHost* guiHostCreate(Canvas& canvas, const char* stylesPath, const char* screenPath);
// Detaches the layer from the canvas it was attached to and frees the instance.
void guiHostDestroy(GuiHost* gui);

// Binds a click action id (the "action" of a clickable node in the screen JSON). Returns false when the bind table is full.
[[nodiscard]] bool guiHostBindClick(GuiHost* gui, uint32_t action, GuiClickFn fn, void* user);
// Binds a draggable node addressed by its entity path ("panel::gain"). fn receives the value in minV..maxV whenever it changes.
[[nodiscard]] bool guiHostBindDrag(GuiHost* gui, const char* path, GuiValueFn fn, void* user);
// Binds an editable node; fn receives the new UTF-8 text whenever it changes.
[[nodiscard]] bool guiHostBindEdit(GuiHost* gui, const char* path, GuiTextFn fn, void* user);

// Pushes application state into a bound draggable / editable node without firing its callback. A draggable that is
// being dragged is left alone.
void guiHostSetValue(GuiHost* gui, const char* path, float value);
void guiHostSetText(GuiHost* gui, const char* path, const char* text);

// Main thread: fires the callbacks queued by the last frame.
void guiHostPump(GuiHost* gui);

} // namespace burnhope
