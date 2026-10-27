#pragma once

#include <cstdint>
#include <vector>

#include "gui/GuiHost.hpp"

namespace burnhope {

struct SceneFrame;

struct HierarchyState {
    std::vector<GuiListEntry> rows; // rows currently pushed to the GUI list, index-aligned with the scene actors
    uint8_t selected = 0xff;        // actor index last mirrored into the list selection
    bool built = false;
    GuiHost* gui = nullptr; // set by hierarchyBind
    SceneFrame* scene = nullptr;
};

// Binds the scene hierarchy list of the right dock: a click selects the actor.
[[nodiscard]] bool hierarchyBind(HierarchyState& h, GuiHost& gui, SceneFrame* scene);
// Every frame: rebuilds the rows when an actor appeared, vanished or was renamed, and mirrors the selection.
void hierarchySync(HierarchyState& h);

} // namespace burnhope
