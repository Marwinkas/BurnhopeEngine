#pragma once

#include "app/BrowserController.hpp"
#include "app/HierarchyController.hpp"
#include "app/InspectorController.hpp"
#include "app/LauncherController.hpp"
#include "app/MaterialController.hpp"
#include "app/ToolbarController.hpp"
#include "gui/GuiHost.hpp"
#include "host/Host.hpp"
#include "app/EditorState.hpp"
#include "render/SceneBlockFill.hpp"
#include "render/SceneFrame.hpp"

namespace burnhope {

struct Engine {
    Host host;
    SceneFrame* scene = nullptr;
    EditorState editor{};
    GuiHost* gui = nullptr;
    InspectorState inspector{};
    BrowserState browser{};
    HierarchyState hierarchy{};
    MaterialState material{};
    LauncherState launcher{};
    ToolbarState toolbar{};
    int slot = -1;
    bool quit = false;
};

[[nodiscard]] bool engineImportScene();
[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
