#include "app/Engine.hpp"
#include "app/SceneBlockFill.hpp"

#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "host/HostNote.hpp"
#include "host/SceneMask.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/Interaction.hpp"
#include "ui/rhi/Canvas.hpp"
#include "ui/Text.hpp"
#include "ui/rhi/UiPhoto.hpp"
#include "Str.hpp"
#include "app/HudBuild.hpp"
#include "asset/AssimpImporter.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "gpu_scene/SceneObject.hpp"
#include "ui/Nodes.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace burnhope {
namespace {

void shellDrop(Host& host, int slot, const char* path, void* user) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    Canvas* canvas = hostCanvas(host, slot);
    if (canvas == nullptr || canvas->state == nullptr) {
        return;
    }
    filesOfferDrop(*canvas, path);
    if (slot >= 0 && std::strcmp(host.view[slot].name, "scene") == 0 && user != nullptr) {
        projectImportModel(*static_cast<HudContext*>(user), path);
    }
}

bool sceneTick(void* user, const InputFrame& input, float dt) {
    auto* view = static_cast<HudContext*>(user);
    if (view == nullptr || view->scene == nullptr) {
        return false;
    }
    InputFrame sceneInput = input;
    const bool lookHeld = view->lookGrab != 0 && (input.pointer.down & kPointerRight) != 0;
    const bool toolHeld = view->toolGrab != 0 && (input.pointer.down & kPointerLeft) != 0;
    if (lookHeld) {
        sceneInput.pointer.x = view->lookX;
        sceneInput.pointer.y = view->lookY;
    }
    Canvas* canvas = view->host != nullptr ? hostCanvas(*view->host, view->slot) : nullptr;
    if (canvas != nullptr && canvas->state != nullptr && canvas->builder != nullptr && !lookHeld && !toolHeld) {
        sceneMaskInput(*canvas->state, sceneInput);
    }
    const bool flyLook = sceneFrameTick(view->scene, sceneInput, dt);
    if (view->host != nullptr && view->slot >= 0) {
        const Swapchain& shown = view->host->view[view->slot].swap;
        if (shown.presentGen != view->presentSeen) {
            view->presentSeen = shown.presentGen;
            sceneFramePresented(view->scene, shown.presentDt);
        }
    }
    if ((input.pointer.pressed & kPointerRight) != 0 && canvas != nullptr && canvas->builder != nullptr && !canvasPointerOver(*canvas)) {
        view->lookGrab = 1;
        view->lookX = input.pointer.x;
        view->lookY = input.pointer.y;
    }
    if ((input.pointer.pressed & kPointerLeft) != 0 && canvas != nullptr && canvas->builder != nullptr && !canvasPointerOver(*canvas)) {
        view->toolGrab = 1;
        view->lookX = input.pointer.x;
        view->lookY = input.pointer.y;
    }
    if (lookHeld && (std::fabs(input.pointer.dx) > 0.5f || std::fabs(input.pointer.dy) > 0.5f)) {
        view->camBreak = 1;
    }
    if ((input.pointer.down & kPointerRight) == 0) {
        view->lookGrab = 0;
    }
    if ((input.pointer.down & kPointerLeft) == 0) {
        view->toolGrab = 0;
    }
    return flyLook || view->lookGrab != 0;
}

void sceneRecord(void* user, VkCommandBuffer cmd, Device& device, DescriptorHeaps& heaps, const Swapchain& swap, const FrameContext& frame) {
    auto* view = static_cast<HudContext*>(user);
    if (view != nullptr && view->scene != nullptr) {
        sceneFrameRecord(view->scene, cmd, device, heaps, swap, frame);
    }
}

void releaseScene(void* user, Device& device) {
    sceneFrameReleaseGpu(static_cast<SceneFrame*>(user), device);
}

} // namespace

void sceneHud(Canvas& canvas, void*);

bool engineImportScene() {
    const char* paths[] = {
        "testscene/assets/bistro/BistroExterior.gltf",
        "testscene/assets/bistro/BistroInterior.gltf",
    };
    std::error_code ec;
    std::filesystem::create_directories("cache", ec);
    if (!sceneImportFbxList(paths, 2, "cache/BistroExterior.bhop.tmp")) {
        return false;
    }
    std::filesystem::rename("cache/BistroExterior.bhop.tmp", "cache/BistroExterior.bhop", ec);
    return !ec;
}

bool engineInit(Engine& e, int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--project") == 0) {
            std::snprintf(e.hud.cliProject, sizeof(e.hud.cliProject), "%s", argv[i + 1]);
        }
    }
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — Vulkan 1.4 core + Yoga/Flecs canvas.");
    if (!hostInit(e.host)) {
        return false;
    }
    e.scene = sceneFrameCreate();
    sceneFrameBlank(e.scene);
    hudSeed(e.hud);
    ViewDesc main{};
    main.name = "scene";
    main.window.title = "Burnhope scene  RMB look  WASD fly";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = true;
    main.window.resizable = true;
    main.frame.present = Present::Immediate;
    main.builder = sceneHud;
    main.onDrop = shellDrop;
    main.sceneTick = sceneTick;
    main.sceneRecord = sceneRecord;
    e.hud.book = &e.colors;
    e.hud.scene = e.scene;
    e.hud.host = &e.host;
    e.hud.slot = -1;
    main.user = &e.hud;
    const int sceneSlot = hostOpen(e.host, main);
    if (sceneSlot < 0) {
        return false;
    }
    e.hud.slot = sceneSlot;
    e.host.releaseGpu = releaseScene;
    e.host.releaseUser = e.scene;
    return true;
}

void engineShutdown(Engine& e) {
    if (e.scene != nullptr) {
        sceneFrameDestroy(e.scene, e.host.device);
        e.scene = nullptr;
    }
    hostShutdown(e.host);
}

bool engineTick(Engine& e) {
    (void)projectGatePoll(e);
    if (e.hud.cliProject[0] != '\0' && e.hud.launcher != kUiNone) {
        char folder[512]{};
        std::snprintf(folder, sizeof(folder), "%s", e.hud.cliProject);
        e.hud.cliProject[0] = '\0';
        projectTake(e.hud, folder, 2);
    }
    hudLauncherSync(e);
    if (e.hud.rootReady != 0) {
        e.hud.rootReady = 0;
        if (e.hud.slot >= 0 && e.hud.projectRoot[0] != '\0') {
            Canvas* canvas = hostCanvas(e.host, e.hud.slot);
            if (canvas != nullptr && canvas->state != nullptr) {
                filesRoot(*canvas->state, e.hud.projectRoot);
            }
        }
    }
    (void)hostSyncColorWindow(e.host, &e.colors);
    const bool ok = hostTick(e.host);
    if (e.scene != nullptr && e.host.primary >= 0) {
        hudFrame(e);
        sceneFrameKick(e.scene, e.host.device);
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok;
}

} // namespace burnhope
