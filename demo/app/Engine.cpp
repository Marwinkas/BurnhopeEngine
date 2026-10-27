#include "app/Engine.hpp"
#include "app/SceneBlockFill.hpp"

#include "platform/PlatformUtil.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "render/SceneFrame.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "asset/AssimpImporter.hpp"

namespace burnhope {
namespace {

// The GUI owns the pointer and the keyboard while it is under the cursor or holds text focus.
void maskInput(const Canvas& canvas, InputFrame& input) {
    if (canvasPointerOver(canvas)) {
        input.pointer.dx = 0.0f;
        input.pointer.dy = 0.0f;
        input.pointer.down = 0;
        input.pointer.pressed = 0;
        input.pointer.released = 0;
        input.clicks = 0;
        input.wheel = 0.0f;
        input.wheelX = 0.0f;
    }
    if (canvas.feedback.wantsText) {
        input.textLen = 0;
        input.backspace = 0;
        input.del = 0;
        input.enter = 0;
        input.tab = 0;
        input.left = 0;
        input.right = 0;
        input.up = 0;
        input.down = 0;
        std::memset(input.keyDown, 0, sizeof(input.keyDown));
        std::memset(input.keyEdge, 0, sizeof(input.keyEdge));
        std::memset(input.keyUp, 0, sizeof(input.keyUp));
    }
}

bool sceneTick(void* user, const InputFrame& input, float dt) {
    auto* e = static_cast<Engine*>(user);
    if (e == nullptr || e->scene == nullptr) {
        return false;
    }
    EditorState& ed = e->editor;
    InputFrame sceneInput = input;
    const bool lookHeld = ed.lookGrab != 0 && (input.pointer.down & kPointerRight) != 0;
    const bool toolHeld = ed.toolGrab != 0 && (input.pointer.down & kPointerLeft) != 0;
    if (lookHeld) {
        sceneInput.pointer.x = ed.lookX;
        sceneInput.pointer.y = ed.lookY;
    }
    Canvas* canvas = hostCanvas(e->host, e->slot);
    const bool overGui = canvas != nullptr && canvasPointerOver(*canvas);
    if (canvas != nullptr && !lookHeld && !toolHeld) {
        maskInput(*canvas, sceneInput);
    }
    const bool flyLook = sceneFrameTick(e->scene, sceneInput, dt);
    if (e->slot >= 0) {
        const Swapchain& shown = e->host.view[e->slot].swap;
        if (shown.presentGen != ed.presentSeen) {
            ed.presentSeen = shown.presentGen;
            sceneFramePresented(e->scene, shown.presentDt);
        }
    }
    if ((input.pointer.pressed & kPointerRight) != 0 && !overGui) {
        ed.lookGrab = 1;
        ed.lookX = input.pointer.x;
        ed.lookY = input.pointer.y;
    }
    if ((input.pointer.pressed & kPointerLeft) != 0 && !overGui) {
        ed.toolGrab = 1;
        ed.lookX = input.pointer.x;
        ed.lookY = input.pointer.y;
    }
    if ((input.pointer.down & kPointerRight) == 0) {
        ed.lookGrab = 0;
    }
    if ((input.pointer.down & kPointerLeft) == 0) {
        ed.toolGrab = 0;
    }
    return flyLook || ed.lookGrab != 0;
}

void sceneRecord(void* user, VkCommandBuffer cmd, Device& device, DescriptorHeaps& heaps, const Swapchain& swap, const FrameContext& frame) {
    auto* e = static_cast<Engine*>(user);
    if (e != nullptr && e->scene != nullptr) {
        sceneFrameRecord(e->scene, cmd, device, heaps, swap, frame);
    }
}

void releaseScene(void* user, Device& device) {
    sceneFrameReleaseGpu(static_cast<SceneFrame*>(user), device);
}

// Pushes the live editor records to the scene, then lets the controllers mirror the scene back into the GUI.
void syncScene(Engine& e) {
    EditorState& ed = e.editor;
    sceneFrameDebug(e.scene, ed.flags.hiz != 0, ed.flags.cone != 0, ed.flags.lod != 0, ed.flags.frust != 0, ed.flags.small != 0,
                    ed.flags.shadowCull != 0, ed.flags.backf != 0, ed.flags.scrn != 0, ed.flags.behind != 0, ed.flags.viewMode);
    sceneBlockFill(ed.block, ed.post, ed.tweaks, ed.flags, ed.actor);
    sceneFrameSetBlock(e.scene, ed.block);
    inspectorSync(e);
    toolbarSync(e);
}

} // namespace

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
    (void)argc;
    (void)argv;
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — Vulkan 1.4 core + Flecs/Yoga GUI.");
    if (!hostInit(e.host)) {
        return false;
    }
    e.scene = sceneFrameCreate();
    sceneFrameBlank(e.scene);
    e.editor.post.grain = 0.0f;
    e.editor.post.vignette = 0.35f;
    e.editor.post.gtao = 1.0f;
    e.editor.tweaks.sscsReach = 1.0f;
    e.editor.tweaks.sscsThick = 0.02f;
    e.editor.tweaks.sscsSteps = 8.0f;
    ViewDesc main{};
    main.name = "scene";
    main.window.title = "Burnhope scene  RMB look  WASD fly";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = true;
    main.window.resizable = true;
    main.frame.present = Present::Immediate;
    main.sceneTick = sceneTick;
    main.sceneRecord = sceneRecord;
    main.user = &e;
    e.slot = hostOpen(e.host, main);
    if (e.slot < 0) {
        return false;
    }
    e.host.releaseGpu = releaseScene;
    e.host.releaseUser = e.scene;

    Canvas* canvas = hostCanvas(e.host, e.slot);
    char stylesPath[512]{};
    char screenPath[512]{};
    if (canvas != nullptr && platformResolveAsset("assets/ui/styles.json", stylesPath, sizeof(stylesPath)) &&
        platformResolveAsset("assets/gui/shell.json", screenPath, sizeof(screenPath))) {
        e.gui = guiHostCreate(*canvas, stylesPath, screenPath);
    }
    if (e.gui == nullptr) {
        spdlog::error("bh_gui screen failed to load");
        return false;
    }
    return inspectorBind(e) && toolbarBind(e) && browserBind(e);
}

void engineShutdown(Engine& e) {
    guiHostDestroy(e.gui);
    e.gui = nullptr;
    if (e.scene != nullptr) {
        sceneFrameDestroy(e.scene, e.host.device);
        e.scene = nullptr;
    }
    hostShutdown(e.host);
}

bool engineTick(Engine& e) {
    guiHostPump(e.gui);
    const bool ok = hostTick(e.host);
    if (e.scene != nullptr && e.host.primary >= 0) {
        syncScene(e);
        sceneFrameKick(e.scene, e.host.device);
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok && !e.quit;
}

} // namespace burnhope
