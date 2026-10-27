#pragma once

#include <cstdint>

#include "gui/data/GuiRow.hpp"
#include "gui/interaction/InteractDragTypes.hpp"
#include "rhi/ui/Canvas.hpp"

namespace burnhope {

// Owns one bh_gui instance (flecs world, style sheet, context) behind an opaque handle, so Layer 3 never sees flecs or Yoga.
// The GUI runs inside Canvas on a worker thread; widget callbacks are queued there and fired by guiHostPump on the main
// thread, so application code never runs concurrently with the draw phase.
struct GuiHost;

using GuiClickFn = void (*)(void* user, uint32_t action);
using GuiValueFn = void (*)(void* user, float value);
using GuiTextFn = void (*)(void* user, const char* text);
// A pooled list item was clicked (activate = false) or double-clicked (activate = true).
using GuiListFn = void (*)(void* user, uint32_t index, bool activate);
// A pooled list item was right-clicked.
using GuiContextFn = void (*)(void* user, uint32_t index);
// A two-axis draggable (x, y in minV..maxV of its range 0..1) changed.
using GuiValue2Fn = void (*)(void* user, float x, float y);

constexpr uint32_t kGuiListTextCap = 128; // bytes (UTF-8), the width of a UiText
constexpr uint32_t kGuiListCap = 1024;           // entries one list can show
constexpr uint32_t kGuiNoSelection = 0xFFFFFFFFu;

// One row of a virtual list. `tone` (0..3) draws a progressively stronger plate behind the row.
// `text` is the plain name; `row` holds the typed tree / table fields drawn around it (indent, chevron, icon, columns).
struct GuiListEntry {
    char text[kGuiListTextCap] = {};
    gui::GuiRow row{};
    uint8_t tone = 0;
    uint8_t thumb = 0; // 1 + atlas cell of the list's thumbnail grid (guiHostSetListThumbs); 0 = no thumbnail
    uint8_t mark = 0;  // 1 = part of a multi-selection, painted like the selected row
    uint8_t pad[13] = {};
};
static_assert(sizeof(GuiListEntry) % 16 == 0);

// Loads the style sheet and the screen layout and installs the GUI as the primitive layer of `canvas`.
// Returns nullptr when either file fails to load; nothing is attached then.
[[nodiscard]] GuiHost* guiHostCreate(Canvas& canvas, const char* stylesPath, const char* screenPath);
// Detaches the layer from the canvas it was attached to and frees the instance.
void guiHostDestroy(GuiHost* gui);

// Binds a click action id (the "action" of a clickable node in the screen JSON). Returns false when the bind table is full.
[[nodiscard]] bool guiHostBindClick(GuiHost* gui, uint32_t action, GuiClickFn fn, void* user);
// Binds a draggable node addressed by its entity path ("panel::gain"). fn receives the value in minV..maxV whenever it changes.
// The node's text at bind time is kept as its caption: the GUI shows "caption  value" and keeps it current.
[[nodiscard]] bool guiHostBindDrag(GuiHost* gui, const char* path, GuiValueFn fn, void* user);
// Binds a draggable with axis "xy": fn receives x and y as 0..1 fractions of the box.
[[nodiscard]] bool guiHostBindDrag2(GuiHost* gui, const char* path, GuiValue2Fn fn, void* user);
// Binds an editable node; fn receives the new UTF-8 text whenever it changes.
[[nodiscard]] bool guiHostBindEdit(GuiHost* gui, const char* path, GuiTextFn fn, void* user);

// Binds a virtual list node ("virtual" behavior with a non-zero "itemAction") addressed by its entity path.
[[nodiscard]] bool guiHostBindList(GuiHost* gui, const char* path, GuiListFn fn, void* user);
// Tooltip text of one item of a bound list ("Name\nType: ..."); out == nullptr asks only whether the item has a tip. The core
// shows it at the pointer after the hover delay (core/gui/overlay/GuiTooltip); nodes with a JSON "tooltip" need no binding.
using GuiTipFn = bool (*)(void* user, uint32_t index, char* out, uint32_t cap);
[[nodiscard]] bool guiHostBindTooltip(GuiHost* gui, const char* path, GuiTipFn fn, void* user);
// Replaces the rows of a bound list (copied; at most kGuiListCap). Main thread only, outside the draw phase.
void guiHostSetList(GuiHost* gui, const char* path, const GuiListEntry* entries, uint32_t count);
void guiHostSetListSelected(GuiHost* gui, const char* path, uint32_t index);
// Swaps the text of every row of a bound list without touching its scroll position or selection (same count).
void guiHostUpdateList(GuiHost* gui, const char* path, const GuiListEntry* entries, uint32_t count);
// Marks or unmarks one row as part of a multi-selection.
void guiHostSetListMark(GuiHost* gui, const char* path, uint32_t index, bool marked);
// Right click on a pooled item of a bound list.
[[nodiscard]] bool guiHostBindListContext(GuiHost* gui, const char* path, GuiContextFn fn, void* user);
// Items with a non-zero GuiListEntry::thumb show cell (thumb - 1) of a cols x rows grid in photo slot `slot` as a square of
// `tilePx` at the top of the item (tile mode). Without a call the list shows no thumbnails.
void guiHostSetListThumbs(GuiHost* gui, const char* path, uint8_t slot, uint8_t cols, uint8_t rows, uint8_t tilePx);
// Numeric constant of the "tokens" block of styles.json ("tile.gutter"); 0 (and a warning) when undefined.
[[nodiscard]] float guiHostToken(const GuiHost* gui, const char* name);
// Icon cell of a registered icon name ("folder"), 0 when unknown.
[[nodiscard]] uint8_t guiHostIconCell(const char* name);
// Style class of the pooled items of a list ("tile_item", "row_item").
void guiHostSetListItemClass(GuiHost* gui, const char* path, const char* styleClass);
// Wheel turns made with Ctrl held over a zoom list are not scrolled but summed for guiHostTakeZoom.
void guiHostSetListZoom(GuiHost* gui, const char* path, bool on);
[[nodiscard]] float guiHostTakeZoom(GuiHost* gui);
// Keyboard modifiers at the last left press: bit 0 Ctrl, bit 1 Shift.
[[nodiscard]] uint8_t guiHostPressMods(const GuiHost* gui);
// True when the last left press landed on the disclosure chevron of row `index` of a list whose rows set GuiRow::chevron.
[[nodiscard]] bool guiHostPressOnChevron(const GuiHost* gui, const char* path, uint32_t index);
// Pointer x (GUI units) at the last left press.
[[nodiscard]] float guiHostPressX(const GuiHost* gui);
// Pointer position and button mask (kPointer*) of the last frame.
void guiHostPointer(const GuiHost* gui, float& x, float& y, uint32_t& down);
// True when the topmost thing under the pointer is exactly the node at `path` (a press there hit no child).
[[nodiscard]] bool guiHostPointerOn(const GuiHost* gui, const char* path);
// True when the topmost thing under the pointer is the node at `path` or one of its descendants.
[[nodiscard]] bool guiHostPointerWithin(const GuiHost* gui, const char* path);
// Generic drag & drop (core/gui InteractDragDrop): `ghost` and `box` are hidden passive nodes (caption next to the pointer,
// outline of the active drop target). The hooks run on the main thread from guiHostDragDropStep.
[[nodiscard]] bool guiHostBindDragDrop(GuiHost* gui, const char* ghost, const char* box, const gui::GuiDragDropOps& ops);
// Once per frame on the main thread, after guiHostPump: advances the gesture from the last frame's pointer.
void guiHostDragDropStep(GuiHost* gui);
// True from the first press on a drag source until the gesture ends.
[[nodiscard]] bool guiHostDragDropBusy(const GuiHost* gui);
// Every `path` argument is an entity path ("bottom::filesCol::grid") or a semantic key: the JSON "bind" of a node
// ("actor.pos.x"), recognised by its '.' and the lack of "::". Keys survive any change of the layout hierarchy.
// Screen box of a node; false when the path is unknown.
[[nodiscard]] bool guiHostBox(const GuiHost* gui, const char* path, float& x, float& y, float& w, float& h);
// Pins a direct child of the screen root at a screen rectangle (selection marquee).
void guiHostSetRect(GuiHost* gui, const char* path, float x, float y, float w, float h);
// Indices of the pooled items of a list whose box meets the screen rectangle; returns how many were written (<= cap).
[[nodiscard]] uint32_t guiHostListHits(const GuiHost* gui, const char* path, float x0, float y0, float x1, float y1, uint32_t* out, uint32_t cap);
// Screen box of the pooled item that shows list row `index`; false when that row is not pooled (scrolled out).
[[nodiscard]] bool guiHostListItemBox(const GuiHost* gui, const char* path, uint32_t index, float& x, float& y, float& w, float& h);
// Hint shown in the muted colour while an editable node is empty and unfocused.
void guiHostSetPlaceholder(GuiHost* gui, const char* path, const char* text);

// Index range of the items currently pooled (shown) by a bound list.
void guiHostListWindow(GuiHost* gui, const char* path, uint32_t& first, uint32_t& live);
// Replaces the thumbnail cell of one row without touching the rest of the list (selection and scroll stay).
void guiHostSetListThumb(GuiHost* gui, const char* path, uint32_t index, uint8_t thumb);
// Item size of a grid list in px; `fitColumns` lets the list use as many columns as its width holds.
void guiHostSetListMetrics(GuiHost* gui, const char* path, float itemW, float itemH, bool fitColumns);
// Opens the overlay at `path` with its anchor on the pointer (context menus).
void guiHostOpenAtPointer(GuiHost* gui, const char* path);
// True when a painted or interactive GUI node covers the point (GUI units) in the current layout.
[[nodiscard]] bool guiHostCovers(GuiHost* gui, float x, float y);

// Parses another screen file and mounts its root below the node at `parentPath` ("" = the screen root). False on any failure.
[[nodiscard]] bool guiHostMount(GuiHost* gui, const char* screenPath, const char* parentPath);
// Shows cell `cell` of a cols x rows grid of photo slot `slot` in a node (slot 255 clears the photo).
void guiHostSetPhoto(GuiHost* gui, const char* path, uint8_t slot, uint8_t cell, uint8_t cols, uint8_t rows);

// Shows or hides a node and its subtree (the space it takes is released too).
void guiHostSetShown(GuiHost* gui, const char* path, bool shown);
// Fills a node with a flat RGB color (a swatch / preview); the node keeps its alpha.
void guiHostSetColor(GuiHost* gui, const char* path, float r, float g, float b);
// Sets the icon of a node by registered name ("check", "chevron_down"); an empty name or nullptr clears it.
void guiHostSetIcon(GuiHost* gui, const char* path, const char* name);
// Paints a button with its class' active color (the "activeBg" token of its style class) or its normal color.
void guiHostSetActive(GuiHost* gui, const char* path, bool active);

// Pushes application state into a bound draggable / editable node without firing its callback. A draggable that is
// being dragged is left alone.
void guiHostSetValue(GuiHost* gui, const char* path, float value);
void guiHostSetText(GuiHost* gui, const char* path, const char* text);

// Main thread: fires the callbacks queued by the last frame.
void guiHostPump(GuiHost* gui);

} // namespace burnhope
