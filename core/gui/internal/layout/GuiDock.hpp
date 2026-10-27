#pragma once

#include <cstdint>
#include <string_view>

#include "data/GuiComponents.hpp"
#include "style/GuiStyleSheet.hpp"

namespace burnhope::gui {

// Tabbed docks (Level 4 trait). A dock is a column that owns a tab bar (its first child, named "tabs", built by the loader)
// and UiDockPanel children, one of which is shown. Every panel has a UiTab in some dock's bar: [icon] [title] [x].
//   - selecting a tab shows its panel and hides the others of the dock;
//   - the close button marks the panel closed (tab and content leave the layout); guiDockOpen brings it back;
//   - a tab moved to another dock's bar takes its panel along, so docks merge and split without any widget class.
// Docks whose panels are all closed hide themselves and come back with their first panel when something shows them again.

constexpr float kGuiTabIconX = 8.0f;   // left edge of the tab icon inside the tab
constexpr float kGuiTabPadL = 28.0f;   // room for the icon before the title
constexpr float kGuiTabPadR = 30.0f;   // room for the close button after the title
constexpr float kGuiTabCloseSide = 16.0f;
constexpr float kGuiTabTextSlack = 4.0f; // keeps the title from wrapping on a rounding difference

// Loader: creates the "tabs" bar as the first child of `dock` (which must already carry UiDock). Returns the bar.
flecs::entity guiDockCreateBar(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity dock);
// Loader: gives `panel` (a child of `dock`) its UiDockPanel and a tab with `title` and icon cell `icon`.
bool guiDockAddPanel(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity dock, flecs::entity panel, std::string_view title,
                     uint8_t icon, bool closed);

// Per frame, before layout: validates the active panel of every dock, shows it and hides the rest, styles the tabs
// (active / idle class), sizes them to their titles and hides the docks that have nothing open.
void guiDockRefresh(flecs::world& world, const GuiStyleSheet& sheet);

// Shows `panel` as the selected one of its dock (opening it when closed and showing the dock).
void guiDockOpen(flecs::world& world, flecs::entity panel);
// Closes `panel`: its tab and content leave the layout.
void guiDockClose(flecs::entity panel);
// Selects an open panel.
void guiDockSelect(flecs::entity panel);

// The tab bar of `dock`; an id-0 entity for a node that is not a dock.
flecs::entity guiDockBar(flecs::entity dock);
// Moves `tab` (and with it its panel) into `bar` at child position `index` (clamped); false when either is not a tab / bar.
bool guiDockMoveTab(flecs::world& world, flecs::entity tab, flecs::entity bar, uint32_t index);
// Child position the dragged `tab` takes in `bar` for a pointer at screen x `px`.
uint32_t guiDockDropIndex(flecs::world& world, flecs::entity tab, flecs::entity bar, float px);

} // namespace burnhope::gui
