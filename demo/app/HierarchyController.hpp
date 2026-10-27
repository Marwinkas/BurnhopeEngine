#pragma once

#include <cstdint>
#include <vector>

#include "host/GuiHost.hpp"

namespace burnhope {

struct Engine;

struct HierarchyState {
    std::vector<GuiListEntry> rows; // rows currently pushed to the GUI list, index-aligned with the scene actors
    uint8_t selected = 0xff;        // actor index last mirrored into the list selection
    bool built = false;
};

// Binds the scene hierarchy list of the right dock: a click selects the actor.
[[nodiscard]] bool hierarchyBind(Engine& e);
// Every frame: rebuilds the rows when an actor appeared, vanished or was renamed, and mirrors the selection.
void hierarchySync(Engine& e);

} // namespace burnhope
