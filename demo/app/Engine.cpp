#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "app/Shell.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "rhi/Device.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Domain.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/PropGrid.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/Interaction.hpp"
#include "ui/State.hpp"
#include "ui/Canvas.hpp"
#include "ui/Text.hpp"
#include "ui/UiPhoto.hpp"
#include "Str.hpp"
#include "app/HudBuild.hpp"
#include "asset/AssimpImporter.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "gpu_scene/SceneObject.hpp"

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
    filesOfferPath(*canvas->state, path);
    if (strIsImagePath(path)) {
        (void)canvasLoadPhoto(*canvas, 0, path);
    } else if (strExtIs(path, ".json")) {
        (void)uiImportJsonFile(*canvas->state, 0, path);
    }
    if (slot >= 0 && std::strcmp(host.view[slot].name, "scene") == 0 && user != nullptr) {
        projectImportModel(*static_cast<HudContext*>(user), path);
    }
}

void onColorClose(Host&, int, void* user) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->colors == nullptr) {
        return;
    }
    demo->colors->present = 0;
    ++demo->colors->gen;
}

void openNamed(Host& host, const ViewDesc& desc) {
    (void)hostOpen(host, desc);
}

void onShellOps(Host& host, int slot, const WindowOps& ops, void* user) {
    (void)ops;
    auto* demo = static_cast<ShellDemo*>(user);
    ColorBook* colors = demo != nullptr ? demo->colors : nullptr;
    Canvas* canvas = hostCanvas(host, slot);
    bool wantTool = false;
    bool wantColor = false;
    bool wantFile = false;
    if (canvas != nullptr && canvas->state != nullptr) {
        UiHostNote& note = uiHostNote(*canvas->state);
        wantTool = note.tool;
        wantFile = note.file;
        note = {};
    }
    bool colorOpen = false;
    for (int i = 0; i < kHostSlots; ++i) {
        if (host.view[i].on && std::strcmp(host.view[i].name, "color") == 0) {
            colorOpen = true;
            break;
        }
    }
    if (colors != nullptr && colors->present == 2 && !colorOpen) {
        wantColor = true;
    }
    if (wantFile && demo != nullptr && slot >= 0 && slot < kHostSlots) {
        static const FileFilter filters[] = {
            {"All files", "*"},
            {"JSON", "json"},
            {"Images", "png;jpg;jpeg;gif;bmp;tga;hdr;psd"},
        };
        if (windowAskFile(host.view[slot].window, filters, 3, FileAsk::Open)) {
            demo->fileDialogSlot = slot;
        }
    }
    if (wantTool) {
        ViewDesc desc{};
        desc.name = "tool";
        desc.window.title = "Burnhope";
        desc.window.width = 560;
        desc.window.height = 420;
        desc.window.minWidth = 360;
        desc.window.minHeight = 240;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = toolBuild;
        desc.onOps = onShellOps;
        desc.user = demo;
        openNamed(host, desc);
    }
    if (wantColor) {
        ViewDesc desc{};
        desc.name = "color";
        desc.window.title = "Color";
        desc.window.width = 440;
        desc.window.height = 720;
        desc.window.minWidth = 360;
        desc.window.minHeight = 480;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = colorBuild;
        desc.onOps = onShellOps;
        desc.onClose = onColorClose;
        desc.user = demo;
        openNamed(host, desc);
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
    if (canvas != nullptr && canvas->builder != nullptr && !lookHeld && !toolHeld) {
        canvasMaskScene(*canvas, sceneInput);
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

bool wantScene(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], "--ui") == 0) {
            return false;
        }
    }
    return true;
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
    (void)argc;
    (void)argv;
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — Vulkan 1.4 core + Yoga/Flecs canvas.");
    if (!hostInit(e.host)) {
        return false;
    }
    if (wantScene(argc, argv)) {
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
        e.demo.colors = &e.colors;
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
        e.hud.projectAsk = 2;
        (void)windowAskFile(e.host.view[sceneSlot].window, nullptr, 0, FileAsk::Folder);
        e.host.releaseGpu = releaseScene;
        e.host.releaseUser = e.scene;
        return true;
    }
    ViewDesc main{};
    main.name = "main";
    main.window.title = "Burnhope";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = false;
    main.window.resizable = true;
    main.frame.present = Present::Mailbox;
    e.demo.colors = &e.colors;
    e.demo.chipsOn = true;
    e.demo.toolsOn = true;
    main.builder = shellBuild;
    main.onDrop = shellDrop;
    main.onOps = onShellOps;
    main.user = &e.demo;
    if (hostOpen(e.host, main) < 0) {
        return false;
    }
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
    char picked[512]{};
    if (!projectGatePoll(e) && windowTakeFile(picked, static_cast<int>(sizeof(picked))) && e.demo.fileDialogSlot >= 0) {
        shellDrop(e.host, e.demo.fileDialogSlot, picked, nullptr);
        e.demo.fileDialogSlot = -1;
    }
    const bool ok = hostTick(e.host);
    if (e.scene != nullptr && e.host.primary >= 0) {
        char fps[96];
        sceneFrameStats(e.scene, fps, sizeof(fps));
        HostMeters meters{};
        platformHostMeters(meters);
        const uint64_t rss = meters.rss;
        const float cpu = meters.cpu;
        const float gpu = meters.gpu;
        const uint64_t vram = deviceMemoryUsed(e.host.device);
        char memLine[96];
        char loadLine[96];
        std::snprintf(memLine, sizeof(memLine), "vram %.0f МБ   ram %.0f МБ", static_cast<double>(vram) / (1024.0 * 1024.0), static_cast<double>(rss) / (1024.0 * 1024.0));
        if (gpu >= 0.0f) {
            std::snprintf(loadLine, sizeof(loadLine), "cpu %.0f%%   gpu %.0f%%", cpu, gpu);
        } else {
            std::snprintf(loadLine, sizeof(loadLine), "cpu %.0f%%   gpu n/a", cpu);
        }
        if (e.host.view[e.host.primary].canvas.state != nullptr) {
            UiState& hudState = *e.host.view[e.host.primary].canvas.state;
            uiText(hudState, e.hud.ids.statsLbl, fps);
            uiText(hudState, e.hud.ids.memLbl, memLine);
            uiText(hudState, e.hud.ids.loadLbl, loadLine);
        }
        Canvas& hud = e.host.view[e.host.primary].canvas;
        const uint8_t selected = sceneFrameActorSelected(e.scene);
        if (e.hud.matOpen == 0 && selected != e.hud.shown && selected != kSceneActorNone) {
            e.hud.shown = selected;
            if (hud.state != nullptr) {
                uiPropShowMask(*hud.state, sceneFrameActorSections(e.scene, selected));
                scrollReset(*hud.state, e.hud.tool.propPage);
            }
            sceneFrameActorGizmo(e.scene, e.hud.toolMode);
        }
        if (hud.state != nullptr) {
            e.hud.bind.sync(*hud.state);
            e.hud.bind.syncSwatches(*hud.state);
            hudMaterialSync(e, hud);
            hudMaterialDrag(e.hud, *hud.state);
            saveOpenMaterial(e.hud);
            hudShowBodies(e.hud, *hud.state);
        }
        syncProp(e, hud);
        uint8_t weatherIndex = kSceneActorNone;
        {
            const uint8_t count = sceneFrameActorCount(e.scene);
            for (uint8_t i = 0; i < count; ++i) {
                if (sceneFrameActorKind(e.scene, i) == static_cast<uint8_t>(SceneActorKind::Weather)) {
                    weatherIndex = i;
                    break;
                }
            }
        }
        hudPushFrame(e, hud, weatherIndex, selected);
        sceneFrameKick(e.scene, e.host.device);
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok;
}

} // namespace burnhope
