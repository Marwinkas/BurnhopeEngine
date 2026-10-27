#pragma once

#include <cstdint>

namespace burnhope {

struct Engine;

struct ToolbarState {
    uint8_t tool = 0;      // gizmo mode: 0 select, 1 move, 2 rotate, 3 scale
    uint8_t speedStep = 2; // index into the camera speed multipliers
    uint8_t frame = 0;
    float baseSpeed = 4.0f;
};

// Binds the top bar (menus, gizmo modes, play / pause, camera and debug view buttons) of shell.json.
[[nodiscard]] bool toolbarBind(Engine& e);
// Every frame: tool / play highlights, button captions and the frame statistics.
void toolbarSync(Engine& e);

} // namespace burnhope
