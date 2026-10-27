#pragma once

namespace burnhope {

struct Engine;

// Binds the file browser's drag & drop hooks to the GUI's generic engine (core/gui InteractDragDrop). The engine owns the
// deadzone, the ghost node and the target outline; this module only says what can be picked up, where it may land and what a
// drop does: a drop on a folder (grid tile or tree row) moves the carried files into it, a drop on the 3D viewport or the
// inspector puts a dragged .bhmat or texture on the picked / selected scene actor.
[[nodiscard]] bool browserDragBind(Engine& e);

// Main thread, once per frame: one step of the drag & drop engine. (The tile tooltip is core: guiHostBindTooltip.)
void browserPointerSync(Engine& e);

} // namespace burnhope
