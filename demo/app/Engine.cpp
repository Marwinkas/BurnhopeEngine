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

constexpr uint32_t kActPlay = 1;
constexpr uint32_t kActPause = 2;
constexpr uint32_t kActView = 3;
constexpr uint32_t kActCamera = 4;
constexpr uint8_t kViewCycle[] = {0, 2, 9, 13};

// Poses the inspector fields drive, in the order of kPoseField.
const char* const kPoseField[9] = {
    "panel::posX", "panel::posY", "panel::posZ", "panel::rotX", "panel::rotY", "panel::rotZ", "panel::sclX", "panel::sclY", "panel::sclZ",
};

float* poseSlot(ActorPose& pose, uint32_t field) {
    return field < 3 ? &pose.pos[field] : (field < 6 ? &pose.euler[field - 3] : &pose.scale[field - 6]);
}

struct FieldBind {
    Engine* engine = nullptr;
    uint32_t field = 0;
};
FieldBind gFieldBind[9];

void onClick(void* user, uint32_t action) {
    auto* e = static_cast<Engine*>(user);
    switch (action) {
    case kActPlay:
        sceneFramePlay(e->scene);
        e->editor.paused = 0;
        break;
    case kActPause:
        e->editor.paused = e->editor.paused != 0 ? 0 : 1;
        sceneFramePause(e->scene, e->editor.paused);
        break;
    case kActView: {
        uint32_t next = 0;
        for (uint32_t i = 0; i < sizeof(kViewCycle); ++i) {
            if (kViewCycle[i] == e->editor.flags.viewMode) {
                next = (i + 1) % sizeof(kViewCycle);
            }
        }
        e->editor.flags.viewMode = kViewCycle[next];
        break;
    }
    case kActCamera:
        sceneFrameCameraProject(e->scene, sceneFrameCameraProject(e->scene) != 0 ? 0 : 1);
        break;
    default:
        break;
    }
}

void onPoseField(void* user, float value) {
    auto* bind = static_cast<FieldBind*>(user);
    Engine& e = *bind->engine;
    const uint8_t selected = sceneFrameActorSelected(e.scene);
    ActorPose pose{};
    if (selected == kSceneActorNone || !sceneFrameActorPose(e.scene, selected, pose)) {
        return;
    }
    *poseSlot(pose, bind->field) = value;
    sceneFrameActorSetPose(e.scene, selected, pose);
}

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

// Pushes the live editor records to the scene and mirrors the selected actor's pose into the inspector fields.
void syncScene(Engine& e) {
    EditorState& ed = e.editor;
    sceneFrameDebug(e.scene, ed.flags.hiz != 0, ed.flags.cone != 0, ed.flags.lod != 0, ed.flags.frust != 0, ed.flags.small != 0,
                    ed.flags.shadowCull != 0, ed.flags.backf != 0, ed.flags.scrn != 0, ed.flags.behind != 0, ed.flags.viewMode);
    sceneBlockFill(ed.block, ed.post, ed.tweaks, ed.flags, ed.actor);
    sceneFrameSetBlock(e.scene, ed.block);
    const uint8_t selected = sceneFrameActorSelected(e.scene);
    ActorPose pose{};
    if (selected == kSceneActorNone || !sceneFrameActorPose(e.scene, selected, pose)) {
        return;
    }
    for (uint32_t i = 0; i < 9; ++i) {
        guiHostSetValue(e.gui, kPoseField[i], *poseSlot(pose, i));
    }
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
        platformResolveAsset("assets/gui/main.json", screenPath, sizeof(screenPath))) {
        e.gui = guiHostCreate(*canvas, stylesPath, screenPath);
    }
    if (e.gui == nullptr) {
        spdlog::error("bh_gui screen failed to load");
        return false;
    }
    bool bound = true;
    for (uint32_t action = kActPlay; action <= kActCamera; ++action) {
        bound = guiHostBindClick(e.gui, action, onClick, &e) && bound;
    }
    for (uint32_t i = 0; i < 9; ++i) {
        gFieldBind[i] = {&e, i};
        bound = guiHostBindDrag(e.gui, kPoseField[i], onPoseField, &gFieldBind[i]) && bound;
    }
    return bound;
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
    return ok;
}

} // namespace burnhope
