#pragma once

// Burnhope editor UI kernel (not the game hot path).
//
// Layers:
//   UICore      POD rect/color/theme/draw instance
//   UIInput     SDL mouse/keys
//   UIRenderer  instanced quads (solid / texture / SDF)
//   UIText      HarfBuzz + FreeType SDF atlas
//   UIWidgets   immediate-mode controls (ImGui-shaped)
//   UIScope     RAII Panel / Child / Clip / ID / Tree / Popup / Menu / Combo
//   UIDockspace fixed editor docking
//
// Write a panel with:
//   ui::Panel panel(widgets, "My Panel", rect);
//   widgets.Text("...");
//   if (widgets.Button("Ok")) { ... }
//
#include "UICore.hpp"
#include "UIInput.hpp"
#include "UIRenderer.hpp"
#include "UIText.hpp"
#include "UIWidgets.hpp"
#include "UIScope.hpp"
#include "UIDockspace.hpp"
