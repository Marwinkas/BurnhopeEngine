#include "app/Engine.hpp"

#include "app/Shell.hpp"
#include "asset/AssimpImporter.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "render/Sharc.hpp"
#include "rhi/Device.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Domain.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Text.hpp"
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

#include <spdlog/spdlog.h>

#include "gpu_scene/SceneObject.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace burnhope {
namespace {

bool extIs(const char* path, const char* ext) {
    const size_t n = std::strlen(path);
    const size_t m = std::strlen(ext);
    if (n < m) {
        return false;
    }
    for (size_t i = 0; i < m; ++i) {
        char a = path[n - m + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool isImagePath(const char* path) {
    return extIs(path, ".png") || extIs(path, ".jpg") || extIs(path, ".jpeg") || extIs(path, ".gif")
        || extIs(path, ".bmp") || extIs(path, ".tga") || extIs(path, ".hdr") || extIs(path, ".psd");
}

void shellDrop(Host& host, int slot, const char* path, void* user) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    Canvas* canvas = hostCanvas(host, slot);
    if (canvas == nullptr || canvas->state == nullptr) {
        return;
    }
    filesOfferPath(*canvas->state, path);
    if (isImagePath(path)) {
        (void)canvasLoadPhoto(*canvas, 0, path);
    } else if (extIs(path, ".json")) {
        (void)uiImportJsonFile(*canvas->state, 0, path);
    }
    (void)user;
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
        if (!std::filesystem::exists("cache/BistroExterior.bhop")) {
            spdlog::info("import testscene/BistroExterior.fbx");
            if (!engineImportScene()) {
                spdlog::error("scene import failed");
            }
        }
        if (!sceneFrameLoadBhop(e.scene, "cache/BistroExterior.bhop")) {
            spdlog::error("cache/BistroExterior.bhop missing");
        }
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

void onActor(void* user, uint16_t) {
    auto* hit = static_cast<ObjectHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->scene == nullptr) {
        return;
    }
    sceneFrameActorSelect(hit->scene, hit->index);
    if (hit->canvas->state != nullptr) {
        uiPropShowMask(*hit->canvas->state, sceneFrameActorSections(hit->scene, hit->index));
    }
}

bool addActorButton(HudContext& hud, uint16_t objects, uint8_t index, const char* name, uint8_t kind) {
    if (hud.tool.canvas == nullptr || objects == kUiNone) {
        return false;
    }
    Canvas& canvas = *hud.tool.canvas;
    if (canvas.state == nullptr) {
        return false;
    }
    uint16_t rowBtn = uiButton(*canvas.state, objects, { .label = name, .fillWidth = true });
    if (rowBtn == kUiNone) {
        return false;
    }
    uiSetHeight(*canvas.state, rowBtn, 28.0f);
    hud.objects.push_back({});
    ObjectHit& hit = hud.objects.back();
    hit.canvas = &canvas;
    hit.scene = hud.tool.scene;
    hit.index = index;
    hit.page = kUiNone;
    if (kind == static_cast<uint8_t>(SceneActorKind::Sun)) {
        hit.page = hud.tool.sunPage;
    } else if (kind == static_cast<uint8_t>(SceneActorKind::Post)) {
        hit.page = hud.tool.postPage;
    } else if (kind == static_cast<uint8_t>(SceneActorKind::Prop)) {
        hit.page = hud.tool.propPage;
    } else if (kind == static_cast<uint8_t>(SceneActorKind::Weather)) {
        hit.page = hud.tool.postPage;
    }
    uiBind(*canvas.state, rowBtn, onActor, &hit);
    return true;
}

void tintIcon(UiState& s, uint16_t icon, bool on) {
    auto* paint = icon < s.count ? s.ent[icon].try_get_mut<UiPaint>() : nullptr;
    if (paint == nullptr) {
        return;
    }
    paint->token = UiToken::Custom;
    paint->r = 1.0f;
    paint->g = on ? 0.533333f : 1.0f;
    paint->b = on ? 0.0f : 1.0f;
    paint->a = 0.0f;
    s.visualDirty = true;
}

uint16_t iconChild(const UiState& s, uint16_t id) {
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == id && s.ent[child].try_get<UiImage>() != nullptr) {
            return child;
        }
    }
    return kUiNone;
}

void paintTools(HudContext& hud) {
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud.tool.canvas->state;
    const uint8_t modes[5] = {3, 0, 1, 2, 4};
    for (int i = 0; i < 5; ++i) {
        const bool on = modes[i] == hud.toolMode;
        tintIcon(s, hud.toolIcon[i], on);
        if (hud.toolIcon[i] < s.count) {
            if (auto* paint = s.ent[hud.toolIcon[i]].try_get_mut<UiPaint>()) {
                paint->bright = on ? 1.0f : 0.7f;
            }
        }
        if (hud.toolBtn[i] < s.count) {
            if (auto* paint = s.ent[hud.toolBtn[i]].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Custom;
                paint->r = 1.0f;
                paint->g = 1.0f;
                paint->b = 1.0f;
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
                paint->radius = 0.0f;
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        tintIcon(s, hud.snapIcon[i], hud.snapOn[i] != 0);
        if (hud.snapBtn[i] < s.count) {
            if (auto* paint = s.ent[hud.snapBtn[i]].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Custom;
                paint->r = 1.0f;
                paint->g = 1.0f;
                paint->b = 1.0f;
                paint->a = hud.snapOn[i] != 0 ? 0.18f : 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
                paint->radius = 0.0f;
            }
        }
    }
}

void onGizmoMode(void* user, uint16_t) {
    auto* hit = static_cast<GizmoHit*>(user);
    if (hit == nullptr || hit->tool == nullptr || hit->tool->scene == nullptr) {
        return;
    }
    sceneFrameActorGizmo(hit->tool->scene, hit->mode);
    if (hit->hud != nullptr) {
        hit->hud->toolMode = hit->mode;
        paintTools(*hit->hud);
    }
}

void onSpace(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->space = index;
    if (hud->tool.scene != nullptr) {
        sceneFrameGizmoSpace(hud->tool.scene, index);
    }
}

void onSnapToggle(void* user, uint16_t) {
    auto* hit = static_cast<GizmoHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->mode > 2) {
        return;
    }
    HudContext& hud = *hit->hud;
    hud.snapOn[hit->mode] = hud.snapOn[hit->mode] == 0 ? 1 : 0;
    paintTools(hud);
    if (hud.tool.scene == nullptr) {
        return;
    }
    sceneFrameSnap(hud.tool.scene, hit->mode, hud.snapOn[hit->mode], hud.snapSent[hit->mode]);
}

void setSlide(Canvas& canvas, uint16_t id, float value);

void onSnapPick(void* user) {
    auto* hit = static_cast<SnapHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->channel > 2) {
        return;
    }
    hit->hud->snapPick[hit->channel] = hit->index;
    hit->hud->snapSent[hit->channel] = hit->step;
    if (hit->hud->tool.canvas != nullptr) {
        setSlide(*hit->hud->tool.canvas, hit->hud->snapNum[hit->channel], hit->step);
    }
    if (hit->hud->tool.scene != nullptr) {
        sceneFrameSnap(hit->hud->tool.scene, hit->channel, hit->hud->snapOn[hit->channel], hit->step);
    }
}

void onPivotSpace(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->pivotSpace = index == 0 ? 0 : 1;
    if (hud->tool.scene != nullptr) {
        sceneFrameGizmoPivot(hud->tool.scene, hud->pivotSpace);
    }
}

void onSpawnKind(void* user) {
    auto* hit = static_cast<SpawnHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->hud->tool.scene == nullptr) {
        return;
    }
    const uint8_t index = sceneFrameSpawn(hit->hud->tool.scene, hit->kind);
    if (index == kSceneActorNone) {
        return;
    }
    char name[24];
    uint8_t kind = 0;
    sceneFrameActorName(hit->hud->tool.scene, index, name, sizeof(name), &kind);
    if (addActorButton(*hit->hud, hit->hud->tool.objects, index, name, kind)) {
        onActor(&hit->hud->objects.back(), 0);
    }
}

void onPlay(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud->tool.canvas->state;
    if (sceneFrameSim(hud->tool.scene) == 0) {
        sceneFramePlay(hud->tool.scene);
        uiShow(s, hud->pauseBtn, true);
        if (auto* paint = s.ent[hud->playGlyph].try_get_mut<UiPaint>()) {
            paint->icon = 19;
        }
    } else {
        sceneFrameStop(hud->tool.scene);
        uiShow(s, hud->pauseBtn, false);
        if (auto* paint = s.ent[hud->playGlyph].try_get_mut<UiPaint>()) {
            paint->icon = 17;
        }
    }
    s.visualDirty = true;
    s.layoutDirty = true;
}

void onPause(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr) {
        return;
    }
    const uint8_t sim = sceneFrameSim(hud->tool.scene);
    sceneFramePause(hud->tool.scene, sim == 1 ? 1 : 0);
}

void onGear(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->statsWant = hud->statsWant == 2 ? 0 : 1;
}

void onCameraButton(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->camWant = hud->camWant == 2 ? 0 : 1;
}

void onMode(void* user);

void onViewButton(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->viewWant = hud->viewWant == 2 ? 0 : 1;
}

void onViewRadio(void* user, uint16_t id) {
    auto* hit = static_cast<ModeHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->canvas->state == nullptr) {
        return;
    }
    uiCheckPress(*hit->canvas->state, id);
    onMode(hit);
    if (hit->hud != nullptr) {
        hit->hud->viewWant = 0;
    }
}

void onSpawnCube(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr) {
        return;
    }
    const uint8_t index = sceneFrameSpawnCube(hud->tool.scene);
    if (index == kSceneActorNone) {
        return;
    }
    char name[24];
    uint8_t kind = 0;
    sceneFrameActorName(hud->tool.scene, index, name, sizeof(name), &kind);
    if (addActorButton(*hud, hud->tool.objects, index, name, kind)) {
        onActor(&hud->objects.back(), 0);
    }
}

void onDuplicate(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr || hud->tool.canvas == nullptr) {
        return;
    }
    const uint8_t index = sceneFrameActorDuplicate(hud->tool.scene);
    if (index == kSceneActorNone) {
        return;
    }
    char name[24];
    uint8_t kind = 0;
    sceneFrameActorName(hud->tool.scene, index, name, sizeof(name), &kind);
    if (addActorButton(*hud, hud->tool.objects, index, name, kind)) {
        onActor(&hud->objects.back(), 0);
    }
}

void setSlide(Canvas& canvas, uint16_t id, float value);
void setMenuCaption(UiState& s, const char* name, const char* text);

void slideName(Canvas& canvas, const char* name, float value) {
    if (canvas.state == nullptr) {
        return;
    }
    setSlide(canvas, uiFindName(*canvas.state, name), value);
}

void applyWeatherPreset(HudContext* hud, uint32_t preset, float coverage, float density, float rain, float wind, const char* caption) {
    if (hud == nullptr || hud->tool.canvas == nullptr) {
        return;
    }
    Canvas& canvas = *hud->tool.canvas;
    slideName(canvas, "clouds", coverage);
    slideName(canvas, "cdens", density);
    slideName(canvas, "rain", rain);
    slideName(canvas, "wind", wind);
    if (canvas.state != nullptr) {
        setMenuCaption(*canvas.state, "wxBtn", caption);
    }
    if (hud->tool.scene != nullptr) {
        sceneFrameWeatherPreset(hud->tool.scene, preset);
    }
}

void wxClear(void* user) { applyWeatherPreset(static_cast<HudContext*>(user), 0u, 0.12f, 0.04f, 0.0f, 2.0f, "Clear"); }
void wxOvercast(void* user) { applyWeatherPreset(static_cast<HudContext*>(user), 1u, 0.70f, 0.10f, 0.4f, 5.0f, "Overcast"); }
void wxRain(void* user) { applyWeatherPreset(static_cast<HudContext*>(user), 2u, 0.82f, 0.12f, 3.0f, 8.0f, "Rain"); }
void wxStorm(void* user) { applyWeatherPreset(static_cast<HudContext*>(user), 3u, 0.95f, 0.18f, 4.0f, 14.0f, "Storm"); }

void setSlide(Canvas& canvas, uint16_t id, float value) {
    if (canvas.state == nullptr || id == kUiNone || id >= canvas.state->count) {
        return;
    }
    auto* range = canvas.state->ent[id].try_get_mut<UiRange>();
    if (range == nullptr || range->value == value) {
        return;
    }
    range->value = value;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(range->value));
    uiSetFieldText(*canvas.state, id, text);
    canvas.state->visualDirty = 1;
}

void applySunShadow(UiState& s, int which);

void setMenuCaption(UiState& s, const char* name, const char* text) {
    const uint16_t id = uiFindName(s, name);
    if (id == kUiNone || id >= s.count) {
        return;
    }
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == id && s.ent[child].try_get<UiText>() != nullptr) {
            uiText(s, child, text);
            return;
        }
    }
}

void applySunShadow(UiState& s, int which) {
    const bool csm = which == 1;
    uiBindCheck(s, uiFindName(s, "sunSh0"), 1, 11, !csm);
    uiBindCheck(s, uiFindName(s, "sunSh1"), 1, 11, csm);
    setMenuCaption(s, "sunShBtn", csm ? "Cascaded Shadow Maps" : "ReSTIR DI Shadows");
}

void sunShRestir(void* user) { applySunShadow(*static_cast<UiState*>(user), 0); }
void sunShCsm(void* user) { applySunShadow(*static_cast<UiState*>(user), 1); }

void applyTone(UiState& s, int which) {
    const char* names[] = {"tone0", "tone1", "tone2", "tone3", "tone4", "tone5", "tone6", "tone7"};
    const char* caps[] = {"AgX", "ACES", "Filmic", "Linear", "PBR Neutral", "GT7", "Generic", "Reinhard"};
    if (which < 0) {
        which = 0;
    }
    if (which > 7) {
        which = 0;
    }
    for (int i = 0; i < 8; ++i) {
        uiBindCheck(s, uiFindName(s, names[i]), 1, 2, i == which);
    }
    setMenuCaption(s, "toneBtn", caps[which]);
}

void toneAgx(void* user) { applyTone(*static_cast<UiState*>(user), 0); }
void toneAces(void* user) { applyTone(*static_cast<UiState*>(user), 1); }
void toneFilmic(void* user) { applyTone(*static_cast<UiState*>(user), 2); }
void toneLinear(void* user) { applyTone(*static_cast<UiState*>(user), 3); }
void toneNeutral(void* user) { applyTone(*static_cast<UiState*>(user), 4); }
void toneGt7(void* user) { applyTone(*static_cast<UiState*>(user), 5); }
void toneGeneric(void* user) { applyTone(*static_cast<UiState*>(user), 6); }
void toneReinhard(void* user) { applyTone(*static_cast<UiState*>(user), 7); }

void showNamed(UiState& s, const char* name, bool on) {
    const uint16_t id = uiFindName(s, name);
    if (id != kUiNone) {
        uiShow(s, id, on);
    }
}

void applyAa(UiState& s, int which) {
    if (which < 0) {
        which = 0;
    }
    if (which > 5) {
        which = 5;
    }
    uiBindCheck(s, uiFindName(s, "smaa"), 0, 0, which == 1);
    uiBindCheck(s, uiFindName(s, "cmaa"), 0, 0, which == 2);
    uiBindCheck(s, uiFindName(s, "fsr"), 0, 0, which == 3);
    uiBindCheck(s, uiFindName(s, "taa"), 0, 0, which == 4);
    uiBindCheck(s, uiFindName(s, "fxaa"), 0, 0, which == 5);
    const char* caps[] = {"Off", "SMAA", "CMAA", "FSR", "TAA", "FXAA"};
    setMenuCaption(s, "aaBtn", caps[which]);
    showNamed(s, "fsrBody", which == 3);
}

void applyFsrQ(UiState& s, int which) {
    const char* names[] = {"fsrq0", "fsrq1", "fsrq2", "fsrq3", "fsrq4"};
    const char* caps[] = {"Native", "Quality", "Balanced", "Performance", "Ultra"};
    if (which < 0) {
        which = 0;
    }
    if (which > 4) {
        which = 4;
    }
    for (int i = 0; i < 5; ++i) {
        uiBindCheck(s, uiFindName(s, names[i]), 1, 4, i == which);
    }
    setMenuCaption(s, "fsrBtn", caps[which]);
}

void aaOff(void* user) { applyAa(*static_cast<UiState*>(user), 0); }
void aaSmaa(void* user) { applyAa(*static_cast<UiState*>(user), 1); }
void aaCmaa(void* user) { applyAa(*static_cast<UiState*>(user), 2); }
void aaFsr(void* user) { applyAa(*static_cast<UiState*>(user), 3); }
void aaTaa(void* user) { applyAa(*static_cast<UiState*>(user), 4); }
void aaFxaa(void* user) { applyAa(*static_cast<UiState*>(user), 5); }
void fsrNative(void* user) { applyFsrQ(*static_cast<UiState*>(user), 0); }
void fsrQuality(void* user) { applyFsrQ(*static_cast<UiState*>(user), 1); }
void fsrBalanced(void* user) { applyFsrQ(*static_cast<UiState*>(user), 2); }
void fsrPerformance(void* user) { applyFsrQ(*static_cast<UiState*>(user), 3); }
void fsrUltra(void* user) { applyFsrQ(*static_cast<UiState*>(user), 4); }

void applyGi(UiState& s, int which) {
    const char* names[] = {"prof0", "prof1", "prof2", "prof3"};
    const char* caps[] = {"None", "VXGI", "Cascades", "Sharc"};
    if (which < 0) {
        which = 0;
    }
    if (which > 3) {
        which = 3;
    }
    for (int i = 0; i < 4; ++i) {
        uiBindCheck(s, uiFindName(s, names[i]), 1, 3, i == which);
    }
    uiBindCheck(s, uiFindName(s, "gien"), 0, 0, which == 3);
    setMenuCaption(s, "giBtn", caps[which]);
    showNamed(s, "indBody", which == 3);
}

void applyDirect(UiState& s, int which) {
    const bool restir = which == 1;
    uiBindCheck(s, uiFindName(s, "restiren"), 0, 0, restir);
    setMenuCaption(s, "diBtn", restir ? "Restir" : "None");
    showNamed(s, "diBody", restir);
}

void applyReflect(UiState& s, int which) {
    if (which < 0) {
        which = 0;
    }
    if (which > 2) {
        which = 2;
    }
    uiBindCheck(s, uiFindName(s, "reflecten"), 0, 0, which != 0);
    uiBindCheck(s, uiFindName(s, "reflRt"), 0, 0, which == 2);
    const char* caps[] = {"None", "SSSR", "SSSR + RT"};
    setMenuCaption(s, "reflBtn", caps[which]);
    showNamed(s, "reflBody", which != 0);
    showNamed(s, "rtBody", which == 2);
}

void applyAo(UiState& s, int which) {
    const bool on = which == 1;
    uiBindCheck(s, uiFindName(s, "gtaoen"), 0, 0, on);
    setMenuCaption(s, "aoBtn", on ? "XeGTAO" : "None");
    showNamed(s, "aoBody", on);
}

void giOff(void* user) { applyGi(*static_cast<UiState*>(user), 0); }
void giRs(void* user) { applyGi(*static_cast<UiState*>(user), 3); }
void diOff(void* user) { applyDirect(*static_cast<UiState*>(user), 0); }
void diRestir(void* user) { applyDirect(*static_cast<UiState*>(user), 1); }
void reflOff(void* user) { applyReflect(*static_cast<UiState*>(user), 0); }
void reflSssr(void* user) { applyReflect(*static_cast<UiState*>(user), 1); }
void reflRt(void* user) { applyReflect(*static_cast<UiState*>(user), 2); }
void aoOff(void* user) { applyAo(*static_cast<UiState*>(user), 0); }
void aoGtao(void* user) { applyAo(*static_cast<UiState*>(user), 1); }

void bindHudIds(HudContext& hud, UiState& state) {
    auto id = [&](const char* name) { return uiFindName(state, name); };
    char name[8];
    for (uint32_t i = 0; i < 20; ++i) {
        std::snprintf(name, sizeof(name), "mode%u", i);
        hud.ids.mode[i] = id(name);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        std::snprintf(name, sizeof(name), "tone%u", i);
        hud.ids.tone[i] = id(name);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        std::snprintf(name, sizeof(name), "prof%u", i);
        hud.ids.prof[i] = id(name);
    }
    for (uint32_t i = 0; i < 40; ++i) {
        std::snprintf(name, sizeof(name), "g%u", i);
        hud.ids.stat[i] = id(name);
    }
    hud.ids.bright = id("bright");
    hud.ids.grain = id("grain");
    hud.ids.bloom = id("bloom");
    hud.ids.vig = id("vig");
    hud.ids.ca = id("ca");
    hud.ids.contact = id("contact");
    hud.ids.spec = id("spec");
    hud.ids.tok = id("tok");
    hud.ids.pom = id("pom");
    hud.ids.ggx = id("ggx");
    hud.ids.gtao = id("gtao");
    hud.ids.lamp = id("lamp");
    hud.ids.sunI = id("sunI");
    hud.ids.sunR = id("sunR");
    hud.ids.sunG = id("sunG");
    hud.ids.sunB = id("sunB");
    hud.ids.lodf = id("lodf");
    hud.ids.gtaor = id("gtaor");
    hud.ids.gtaoSt = id("gtaoSt");
    hud.ids.gtaoSl = id("gtaoSl");
    hud.ids.gtaoPw = id("gtaoPw");
    hud.ids.gtaoFa = id("gtaoFa");
    hud.ids.sscsR = id("sscsR");
    hud.ids.sscsT = id("sscsT");
    hud.ids.sscsS = id("sscsS");
    hud.ids.sscsP = id("sscsP");
    hud.ids.rcRays = id("rcRays");
    hud.ids.rcCell = id("rcCell");
    hud.ids.rcGain = id("rcGain");
    hud.ids.giGain = id("giGain");
    hud.ids.denoise = id("denoise");
    hud.ids.rcMax = id("rcMax");
    hud.ids.rcSun = id("rcSun");
    hud.ids.rcBounce = id("rcBounce");
    hud.ids.rsLocal = id("rsLocal");
    hud.ids.rsSpatial = id("rsSpatial");
    hud.ids.rsRadius = id("rsRadius");
    hud.ids.rsHistory = id("rsHistory");
    hud.ids.rsBoil = id("rsBoil");
    hud.ids.rsTDepth = id("rsTDepth");
    hud.ids.rsTNorm = id("rsTNorm");
    hud.ids.rsTBias = id("rsTBias");
    hud.ids.rsSDepth = id("rsSDepth");
    hud.ids.rsSNorm = id("rsSNorm");
    hud.ids.rsSBias = id("rsSBias");
    hud.ids.rsSBoost = id("rsSBoost");
    hud.ids.rsSun = id("rsSun");
    hud.ids.rsSunRays = id("rsSunRays");
    hud.ids.rsShadowRays = id("rsShadowRays");
    hud.ids.rsShadowRange = id("rsShadowRange");
    hud.ids.rsShadowBias = id("rsShadowBias");
    hud.ids.rsPermTh = id("rsPermTh");
    hud.ids.rsTarget = id("rsTarget");
    hud.ids.rsTOn = id("rsTOn");
    hud.ids.rsSOn = id("rsSOn");
    hud.ids.rsBoilOn = id("rsBoilOn");
    hud.ids.rsVis = id("rsVis");
    hud.ids.rsPerm = id("rsPerm");
    hud.ids.rsShort = id("rsShort");
    hud.ids.rsMat = id("rsMat");
    hud.ids.rsNaive = id("rsNaive");
    hud.ids.rsSunOn = id("rsSunOn");
    hud.ids.restiren = id("restiren");
    hud.ids.bloomth = id("bloomth");
    hud.ids.fog = id("fog");
    hud.ids.fogH = id("fogH");
    hud.ids.fogS = id("fogS");
    hud.ids.fogFall = id("fogFall");
    hud.ids.fogR = id("fogR");
    hud.ids.fogG = id("fogG");
    hud.ids.fogB = id("fogB");
    hud.ids.fogLum = id("fogLum");
    hud.ids.fogStart = id("fogStart");
    hud.ids.fogOp = id("fogOp");
    hud.ids.fogIn = id("fogIn");
    hud.ids.rain = id("rain");
    hud.ids.rainWet = id("rainWet");
    hud.ids.rainRip = id("rainRip");
    hud.ids.wind = id("wind");
    hud.ids.windYaw = id("windYaw");
    hud.ids.wxTime = id("wxTime");
    hud.ids.wxDay = id("wxDay");
    hud.ids.wxTemp = id("wxTemp");
    hud.ids.wxClock = id("wxClock");
    hud.ids.zenith = id("zenith");
    hud.ids.azimuth = id("azimuth");
    hud.ids.clouds = id("clouds");
    hud.ids.cdens = id("cdens");
    hud.ids.hiz = id("hiz");
    hud.ids.cone = id("cone");
    hud.ids.lod = id("lod");
    hud.ids.frust = id("frust");
    hud.ids.small = id("small");
    hud.ids.backface = id("backf");
    hud.ids.screen = id("scrn");
    hud.ids.behind = id("behind");
    hud.ids.shadow = id("shadow");
    hud.ids.cluster = id("cluster");
    hud.ids.probes = id("probes");
    hud.ids.gien = id("gien");
    hud.ids.cmaa = id("cmaa");
    hud.ids.fsr = id("fsr");
    hud.ids.fsrq[0] = id("fsrq0");
    hud.ids.fsrq[1] = id("fsrq1");
    hud.ids.fsrq[2] = id("fsrq2");
    hud.ids.fsrq[3] = id("fsrq3");
    hud.ids.fsrq[4] = id("fsrq4");
    hud.ids.fsrSharp = id("fsrSharp");
    hud.ids.gtaoen = id("gtaoen");
    hud.ids.gtaoDn = id("gtaoDn");
    hud.ids.bloomen = id("bloomen");
    hud.ids.smaa = id("smaa");
    hud.ids.fxaa = id("fxaa");
    hud.ids.taa = id("taa");
    hud.ids.lumScale = id("lumScale");
    hud.ids.gamut = id("gamut");
    hud.ids.exposure = id("exposure");
    hud.ids.temperature = id("temperature");
    hud.ids.tint = id("tint");
    hud.ids.contrast = id("contrast");
    hud.ids.saturation = id("saturation");
    hud.ids.night = id("night");
    hud.ids.shadows = id("shadows");
    hud.ids.midtones = id("midtones");
    hud.ids.highlights = id("highlights");
    hud.ids.slope = id("slope");
    hud.ids.cdlOffset = id("cdlOffset");
    hud.ids.power = id("power");
    hud.ids.dofFocus = id("dofFocus");
    hud.ids.dofAmount = id("dofAmount");
    hud.ids.flare = id("flare");
    hud.ids.mix = id("mix");
    hud.ids.genContrast = id("genContrast");
    hud.ids.midIn = id("midIn");
    hud.ids.midOut = id("midOut");
    hud.ids.hdrMax = id("hdrMax");
    hud.ids.vibrance = id("vibrance");
    hud.ids.sun = id("sun");
    hud.ids.points = id("points");
    hud.ids.spots = id("spots");
    hud.ids.area = id("area");
    hud.ids.sky = id("sky");
    hud.ids.clouden = id("clouden");
    hud.ids.fogen = id("fogen");
    hud.ids.reflecten = id("reflecten");
    hud.ids.ssrCut = id("ssrCut");
    hud.ids.ssrSteps = id("ssrSteps");
    hud.ids.ssrThick = id("ssrThick");
    hud.ids.ssrMax = id("ssrMax");
    hud.ids.ssrBlur = id("ssrBlur");
    hud.ids.ssrMetal = id("ssrMetal");
    hud.ids.ssrRt = id("ssrRt");
    hud.ids.ssrEdge = id("ssrEdge");
    hud.ids.pbrR = id("pbrR");
    hud.ids.pbrG = id("pbrG");
    hud.ids.pbrB = id("pbrB");
    hud.ids.pbrRough = id("pbrRough");
    hud.ids.pbrMetal = id("pbrMetal");
    hud.ids.pbrEm = id("pbrEm");
    hud.ids.pbrPx = id("pbrPx");
    hud.ids.pbrPy = id("pbrPy");
    hud.ids.pbrPz = id("pbrPz");
    hud.ids.pbrEx = id("pbrEx");
    hud.ids.pbrEy = id("pbrEy");
    hud.ids.pbrEz = id("pbrEz");
    hud.ids.pbrSx = id("pbrSx");
    hud.ids.pbrSy = id("pbrSy");
    hud.ids.pbrSz = id("pbrSz");
    hud.ids.pbrOx = id("pbrOx");
    hud.ids.pbrOy = id("pbrOy");
    hud.ids.pbrOz = id("pbrOz");
    hud.ids.xfStatic = id("xfStatic");
    hud.ids.reflRt = id("reflRt");
    hud.ids.fogSwatch = id("fogSwatch");
    hud.ids.sunSwatch = id("sunSwatch");
    hud.ids.sunCast = id("sunCast");
    hud.ids.sunKelvin = id("sunK");
    hud.ids.sunAngle = id("sunAngle");
    hud.ids.sunSoft = id("sunSoft");
    hud.ids.sunIndirect = id("sunIndirect");
    hud.ids.sunSh0 = id("sunSh0");
    hud.ids.sunSh1 = id("sunSh1");
    hud.ids.contacten = id("contacten");
    hud.ids.vt = id("vt");
    hud.ids.freeze = id("freeze");
    hud.ids.rcen = id("rcen");
}

struct ViewMode {
    int index;
    const char* name;
};

const ViewMode kViews[] = {
    {0, "Lit"}, {1, "Primitive ID"}, {2, "Normals"}, {4, "GI"}, {5, "каскады"}, {6, "контакт"},
    {7, "луч"}, {8, "прямой"}, {9, "альбедо"}, {10, "солнце"}, {11, "шероховатость"},
    {12, "металл"}, {13, "AO"}, {14, "свечение"},
    {15, "ReSTIR свет"}, {16, "ReSTIR вес"}, {17, "ReSTIR M"}, {18, "ReSTIR тень"}, {19, "ReSTIR индекс"},
};

void onDuplicateCmd(void* user) {
    onDuplicate(user, 0);
}

void onStats(void* user, uint16_t) {
    auto* hit = static_cast<StatsHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->canvas->state == nullptr || hit->page == kUiNone) {
        return;
    }
    hit->open = hit->open == 0 ? 1 : 0;
    uiShow(*hit->canvas->state, hit->page, hit->open != 0);
    auto* flex = hit->canvas->state->ent[hit->dock].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->width += hit->open != 0 ? 420.0f : -420.0f;
        if (flex->width < 300.0f) {
            flex->width = 300.0f;
        }
    }
}

void onStatsCmd(void* user) {
    onStats(user, 0);
}

void onMode(void* user) {
    auto* hit = static_cast<ModeHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr) {
        return;
    }
    for (const ViewMode& mode : kViews) {
        char name[8];
        std::snprintf(name, sizeof(name), "mode%d", mode.index);
        if (hit->canvas->state != nullptr) {
            uiBindCheck(*hit->canvas->state, uiFindName(*hit->canvas->state, name), 1, 1, mode.index == hit->index);
        }
    }
    const char* title = "Lit";
    for (const ViewMode& mode : kViews) {
        if (mode.index == hit->index) {
            title = mode.name;
        }
    }
    if (hit->hud != nullptr && hit->canvas->state != nullptr) {
        for (const ModeHit& mode : hit->hud->modes) {
            if (mode.radio < hit->canvas->state->count) {
                if (auto* check = hit->canvas->state->ent[mode.radio].try_get_mut<UiCheck>()) {
                    check->on = mode.index == hit->index ? 1 : 0;
                }
            }
        }
    }
    if (hit->label != kUiNone && hit->canvas->state != nullptr) {
        UiState& s = *hit->canvas->state;
        uiText(s, hit->label, title);
        uiTextStyle(s, hit->label, UiTextAlign::Left, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        const uint16_t button = uiParent(s, hit->label);
        uint8_t len = 0;
        while (title[len] != '\0' && len < 48) {
            ++len;
        }
        const float width = uiMeasure(s, title, len, len) + 38.0f;
        if (button < s.count) {
            if (auto* flex = s.ent[button].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = width;
                flex->grow = 0.0f;
                flex->shrink = 0.0f;
            }
        }
        s.layoutDirty = true;
    }
}

float slideAt(const Canvas& canvas, uint16_t id, float fallback);

void openSwatch(HudContext* hud, uint16_t id, const char* rName, const char* gName, const char* bName, float r0, float g0, float b0) {
    if (hud == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud->tool.canvas->state;
    ColorBook* book = uiColor(s).book;
    if (book != nullptr) {
        book->r = slideAt(*hud->tool.canvas, uiFindName(s, rName), r0);
        book->g = slideAt(*hud->tool.canvas, uiFindName(s, gName), g0);
        book->b = slideAt(*hud->tool.canvas, uiFindName(s, bName), b0);
        book->a = 1.0f;
        book->gen += 1;
    }
    colorOnOpen(&s, id);
}

void onFogSwatch(void* user, uint16_t id) {
    openSwatch(static_cast<HudContext*>(user), id, "fogR", "fogG", "fogB", 0.62f, 0.72f, 0.86f);
}

void onSunSwatch(void* user, uint16_t id) {
    openSwatch(static_cast<HudContext*>(user), id, "sunR", "sunG", "sunB", 1.0f, 0.92f, 0.76f);
}

uint16_t rowFine(UiState& s, uint16_t panel, const char* text, const char* name, float value, float minV, float maxV, float step) {
    const uint16_t id = uiPropNumber(s, panel, text, value, minV, maxV, step);
    uiName(s, id, name);
    return id;
}

uint16_t rowSlide(UiState& s, uint16_t panel, const char* text, const char* name, float value, float minV, float maxV) {
    const float span = maxV - minV;
    float step = 0.01f;
    if (span > 40.0f) {
        step = 1.0f;
    } else if (span > 4.0f) {
        step = 0.1f;
    }
    const uint16_t id = uiPropNumber(s, panel, text, value, minV, maxV, step);
    uiName(s, id, name);
    return id;
}

bool loadToolIcon(Canvas& canvas, uint8_t slot, const char* file) {
    char rel[160]{};
    std::snprintf(rel, sizeof(rel), "assets/icons/%s", file);
    if (canvasLoadPhoto(canvas, slot, rel)) {
        return true;
    }
    std::snprintf(rel, sizeof(rel), "../assets/icons/%s", file);
    if (canvasLoadPhoto(canvas, slot, rel)) {
        return true;
    }
    char dir[512]{};
    if (!platformExePath(dir, sizeof(dir))) {
        return false;
    }
    for (int up = 0; up < 5; ++up) {
        char parent[512]{};
        if (!pathParent(dir, parent, sizeof(parent))) {
            break;
        }
        strCopyCap(dir, sizeof(dir), parent);
        char face[640]{};
        std::snprintf(face, sizeof(face), "%s/assets/icons/%s", dir, file);
        if (canvasLoadPhoto(canvas, slot, face)) {
            return true;
        }
    }
    return false;
}

bool fontBesideExe(Canvas& canvas) {
    char dir[512]{};
    if (!platformExePath(dir, sizeof(dir))) {
        return false;
    }
    for (int up = 0; up < 5; ++up) {
        char parent[512]{};
        if (!pathParent(dir, parent, sizeof(parent))) {
            break;
        }
        strCopyCap(dir, sizeof(dir), parent);
        char face[640]{};
        std::snprintf(face, sizeof(face), "%s/assets/fonts/Inter-Regular.ttf", dir);
        if (canvasSetFont(canvas, face, 20.0f)) {
            return true;
        }
    }
    return false;
}

void sceneHud(Canvas& canvas, void* user) {
    auto* hud = static_cast<HudContext*>(user);
    const bool haveFont = canvas.state != nullptr && canvas.state->font.ready;
    if (hud == nullptr || (!haveFont && !fontBesideExe(canvas) && !canvasUseFont(canvas, 20.0f)) || canvas.state == nullptr) {
        return;
    }
    uiInstallColorBin(*canvas.state);
    if (hud->book != nullptr) {
        uiSetColorBook(*canvas.state, hud->book);
    }
    hud->pages.clear();
    hud->modes.clear();
    hud->gizmos.clear();
    hud->snaps.clear();
    hud->objects.clear();
    hud->shown = 0xff;
    hud->tool = {};
    hud->tool.canvas = &canvas;
    hud->tool.scene = hud->scene;
    hud->stats = {};
    filesLoadIcons(canvas);
    canvasSetLook(canvas, uiLookInk());
    const UiLook look = canvasLook(canvas);
    auto hiddenPaint = []() {
        UiPaint paint{};
        paint.a = 0.0f;
        return paint;
    };
    auto dockPaint = [&]() {
        UiPaint paint{};
        paint.r = look.content[0];
        paint.g = look.content[1];
        paint.b = look.content[2];
        paint.a = 0.94f;
        paint.borderW = 0.0f;
        return paint;
    };
    auto veilPaint = [&]() {
        UiPaint paint{};
        paint.r = look.content[0];
        paint.g = look.content[1];
        paint.b = look.content[2];
        paint.a = 0.72f;
        paint.borderW = 1.0f;
        paint.br = look.muted[0];
        paint.bg = look.muted[1];
        paint.bb = look.muted[2];
        paint.radius = look.radiusSm;
        return paint;
    };
    auto splitBar = [&](uint16_t parent, bool vertical) {
        return uiSplitBar(*canvas.state, parent, vertical);
    };
    auto scrollPage = [&](uint16_t parent, bool show) {
        UiFlex viewFlex{};
        viewFlex.grow = 1.0f;
        viewFlex.shrink = 1.0f;
        viewFlex.minH = 0.0f;
        viewFlex.overflow = UiOverflow::Scroll;
        UiPaint scroll{};
        scroll.r = look.content[0];
        scroll.g = look.content[1];
        scroll.b = look.content[2];
        scroll.a = 1.0f;
        const uint16_t view = uiPanel(*canvas.state, parent, viewFlex, scroll);
        UiFlex column{};
        column.gap = 4.0f;
        column.pad = 8.0f;
        column.shrink = 0.0f;
        const uint16_t inner = uiPanel(*canvas.state, view, column, hiddenPaint());
        if (canvas.state != nullptr) {
            uiBindScroll(*canvas.state, view, inner);
        }
        if (!show) {
            uiShow(*canvas.state, view, false);
        }
        return inner;
    };
    UiFlex rootFlex{};
    rootFlex.grow = 1.0f;
    rootFlex.direction = UiFlexDir::Column;
    rootFlex.widthMode = UiSize::Percent;
    rootFlex.width = 100.0f;
    rootFlex.heightMode = UiSize::Percent;
    rootFlex.height = 100.0f;
    const uint16_t root = uiPanel(*canvas.state, kUiNone, rootFlex, hiddenPaint());
    UiFlex menuFlex{};
    menuFlex.direction = UiFlexDir::Row;
    menuFlex.heightMode = UiSize::Px;
    menuFlex.height = 32.0f;
    menuFlex.minH = 32.0f;
    menuFlex.shrink = 0.0f;
    menuFlex.align = UiAlign::Center;
    menuFlex.padL = 8.0f;
    menuFlex.padR = 8.0f;
    menuFlex.gap = 4.0f;
    const uint16_t menuBar = uiPanel(*canvas.state, root, menuFlex, dockPaint());
    uint16_t fileBtn = uiButton(*canvas.state, menuBar, { .label = "File" });
    uiSetHeight(*canvas.state, fileBtn, 26.0f);
    uint16_t fileMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state) {
        uiSetContextMenu(*canvas.state, fileBtn, fileMenu);
        uiMarkMenuBar(*canvas.state, fileBtn);
    }
    auto fileAct = [](void* actUser) {
        auto* hit = static_cast<FileAct*>(actUser);
        if (hit == nullptr || hit->canvas == nullptr || hit->canvas->state == nullptr) {
            return;
        }
        char home[512]{};
        char path[512]{};
        if (!platformUserDir(home, sizeof(home))) {
            home[0] = '.';
            home[1] = '\0';
        }
        std::snprintf(path, sizeof(path), "%s/BurnhopeProjects/Untitled", home);
        if (hit->kind == 0) {
            filesNewProject(*hit->canvas->state, path);
        } else if (hit->kind == 1) {
            filesOpenProject(*hit->canvas->state, path);
        } else if (hit->kind == 3) {
            std::exit(0);
        }
    };
    hud->fileNew = {&canvas, 0};
    hud->fileOpen = {&canvas, 1};
    hud->fileSave = {&canvas, 2};
    hud->fileExit = {&canvas, 3};
    if (canvas.state != nullptr) {
        (void)uiCommandAddTo(*canvas.state, fileMenu, "New Project...", fileAct, &hud->fileNew);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Open Project...", fileAct, &hud->fileOpen);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Save Scene", fileAct, &hud->fileSave);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Exit", fileAct, &hud->fileExit);
    }
    uint16_t editBtn = uiButton(*canvas.state, menuBar, { .label = "Edit" });
    uiSetHeight(*canvas.state, editBtn, 26.0f);
    uint16_t editMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state != nullptr) {
        uiSetContextMenu(*canvas.state, editBtn, editMenu);
        uiMarkMenuBar(*canvas.state, editBtn);
        (void)uiCommandAddTo(*canvas.state, editMenu, "Duplicate", onDuplicateCmd, hud);
    }
    uint16_t viewMenuBtn = uiButton(*canvas.state, menuBar, { .label = "View" });
    uiSetHeight(*canvas.state, viewMenuBtn, 26.0f);
    uint16_t appViewMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state != nullptr) {
        uiSetContextMenu(*canvas.state, viewMenuBtn, appViewMenu);
        uiMarkMenuBar(*canvas.state, viewMenuBtn);
        (void)uiCommandAddTo(*canvas.state, appViewMenu, "Stats", onStatsCmd, &hud->stats);
    }
    UiFlex rowFlex{};
    rowFlex.direction = UiFlexDir::Row;
    rowFlex.grow = 1.0f;
    rowFlex.shrink = 1.0f;
    rowFlex.minH = 72.0f;
    const uint16_t row = uiPanel(*canvas.state, root, rowFlex, hiddenPaint());
    UiFlex viewColFlex{};
    viewColFlex.direction = UiFlexDir::Column;
    viewColFlex.grow = 1.0f;
    viewColFlex.shrink = 1.0f;
    viewColFlex.minW = 0.0f;
    const uint16_t viewCol = uiPanel(*canvas.state, row, viewColFlex, hiddenPaint());
    (void)loadToolIcon(canvas, 2, "SelectMode.png");
    (void)loadToolIcon(canvas, 3, "MoveMode.png");
    (void)loadToolIcon(canvas, 4, "RotateMode.png");
    (void)loadToolIcon(canvas, 5, "ScallingMode.png");
    (void)loadToolIcon(canvas, 6, "Camera.png");
    (void)loadToolIcon(canvas, 7, "Snap.png");
    UiFlex toolFlex{};
    toolFlex.direction = UiFlexDir::Row;
    toolFlex.heightMode = UiSize::Px;
    toolFlex.height = 36.0f;
    toolFlex.minH = 36.0f;
    toolFlex.shrink = 0.0f;
    toolFlex.align = UiAlign::Center;
    toolFlex.padL = 6.0f;
    toolFlex.padR = 6.0f;
    toolFlex.gap = 6.0f;
    const uint16_t toolbar = uiPanel(*canvas.state, viewCol, toolFlex, dockPaint());
    UiFlex toolLeftFlex{};
    toolLeftFlex.direction = UiFlexDir::Row;
    toolLeftFlex.grow = 1.0f;
    toolLeftFlex.shrink = 1.0f;
    toolLeftFlex.minW = 0.0f;
    toolLeftFlex.align = UiAlign::Center;
    toolLeftFlex.gap = 4.0f;
    toolLeftFlex.overflow = UiOverflow::Hidden;
    const uint16_t toolLeft = uiPanel(*canvas.state, toolbar, toolLeftFlex, hiddenPaint());
    UiFlex toolRightFlex{};
    toolRightFlex.direction = UiFlexDir::Row;
    toolRightFlex.shrink = 0.0f;
    toolRightFlex.align = UiAlign::Center;
    toolRightFlex.gap = 4.0f;
    const uint16_t toolRight = uiPanel(*canvas.state, toolbar, toolRightFlex, hiddenPaint());
    auto toolSep = [&](uint16_t parent) {
        UiPaint ink = paintToken(look, UiToken::Muted, 0.0f);
        ink.a = 0.55f;
        return uiPanel(*canvas.state, parent, px(1.0f, 18.0f), ink);
    };
    {
        uint16_t plus = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f });
        hud->spawnBtn = plus;
        if (plus < canvas.state->count) {
            if (auto* flex = canvas.state->ent[plus].try_get_mut<UiFlex>()) {
                flex->align = UiAlign::Center;
                flex->justify = UiJustify::Center;
                flex->pad = 0.0f;
                flex->padL = 0.0f;
                flex->padR = 0.0f;
                flex->padT = 0.0f;
                flex->padB = 0.0f;
            }
            if (auto* paint = canvas.state->ent[plus].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Custom;
                paint->icon = 16;
                paint->r = 1.0f;
                paint->g = 1.0f;
                paint->b = 1.0f;
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
                paint->radius = 0.0f;
            }
        }
        uint16_t menu = uiMakeMenu(*canvas.state);
        if (menu != kUiNone) {
            canvas.state->ent[menu].add<UiMenuSlide>();
        }
        const char* names[] = {"Cube", "Sphere", "Plane", "Cylinder", "Directional Light", "Point Light", "Empty Actor"};
        for (uint8_t i = 0; i < 7; ++i) {
            hud->spawns.push_back({});
            SpawnHit& hit = hud->spawns.back();
            hit.hud = hud;
            hit.kind = i;
            if (menu != kUiNone) {
                (void)uiCommandAddTo(*canvas.state, menu, names[i], onSpawnKind, &hit);
            }
        }
        uiSetContextMenu(*canvas.state, plus, menu);
        uiMarkMenuBar(*canvas.state, plus);
    }
    (void)toolSep(toolLeft);
    const uint8_t toolModes[5] = {3, 0, 1, 2, 4};
    const uint8_t toolIcons[4] = {2, 3, 4, 5};
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t toolBtn = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f, .iconSlot = toolIcons[i] });
        hud->toolBtn[i] = toolBtn;
        hud->toolIcon[i] = iconChild(*canvas.state, toolBtn);
        if (toolBtn < canvas.state->count) {
            if (auto* flex = canvas.state->ent[toolBtn].try_get_mut<UiFlex>()) {
                flex->pad = 0.0f;
                flex->padL = 0.0f;
                flex->padR = 0.0f;
                flex->padT = 0.0f;
                flex->padB = 0.0f;
                flex->align = UiAlign::Center;
                flex->justify = UiJustify::Center;
            }
        }
        if (hud->toolIcon[i] < canvas.state->count) {
            if (auto* flex = canvas.state->ent[hud->toolIcon[i]].try_get_mut<UiFlex>()) {
                flex->width = 28.0f;
                flex->height = 28.0f;
            }
        }
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.hud = hud;
        gizmo.mode = toolModes[i];
        uiBind(*canvas.state, toolBtn, onGizmoMode, &gizmo);
    }
    {
        uint16_t toolBtn = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f });
        hud->toolBtn[4] = toolBtn;
        if (toolBtn < canvas.state->count) {
            if (auto* flex = canvas.state->ent[toolBtn].try_get_mut<UiFlex>()) {
                flex->pad = 0.0f;
                flex->padL = 0.0f;
                flex->padR = 0.0f;
                flex->padT = 0.0f;
                flex->padB = 0.0f;
                flex->align = UiAlign::Center;
                flex->justify = UiJustify::Center;
            }
        }
        UiFlex mark{};
        mark.widthMode = UiSize::Px;
        mark.width = 28.0f;
        mark.heightMode = UiSize::Px;
        mark.height = 28.0f;
        mark.shrink = 0.0f;
        UiPaint glyph = paintClear();
        glyph.icon = 15;
        glyph.r = 1.0f;
        glyph.g = 1.0f;
        glyph.b = 1.0f;
        hud->toolIcon[4] = uiPanel(*canvas.state, toolBtn, mark, glyph);
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.hud = hud;
        gizmo.mode = 4;
        uiBind(*canvas.state, toolBtn, onGizmoMode, &gizmo);
    }
    if (hud->tool.scene != nullptr) {
        sceneFrameActorGizmo(hud->tool.scene, hud->toolMode);
    }
    paintTools(*hud);
    (void)toolSep(toolLeft);
    auto fitChoice = [&](const char* const* labels, int count) {
        float widest = 0.0f;
        for (int i = 0; i < count; ++i) {
            uint8_t len = 0;
            while (labels[i] != nullptr && labels[i][len] != '\0' && len < 48) {
                ++len;
            }
            const float measured = uiMeasure(*canvas.state, labels[i], len, len);
            if (measured > widest) {
                widest = measured;
            }
        }
        return widest + 26.0f;
    };
    auto slideChoice = [&](uint16_t button, float width) {
        if (button >= canvas.state->count) {
            return;
        }
        if (auto* flex = canvas.state->ent[button].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = width;
            flex->minW = 0.0f;
            flex->grow = 0.0f;
            flex->shrink = 0.0f;
            flex->padL = 6.0f;
            flex->padR = 4.0f;
            flex->gap = 2.0f;
        }
        const UiChoice* choice = canvas.state->ent[button].try_get<UiChoice>();
        if (choice != nullptr && choice->menu < canvas.state->count) {
            canvas.state->ent[choice->menu].add<UiMenuSlide>();
            if (auto* flex = canvas.state->ent[choice->menu].try_get_mut<UiFlex>()) {
                flex->width = width;
            }
        }
    };
    const char* spaceLabels[] = {"Local", "World"};
    const float spaceW = fitChoice(spaceLabels, 2);
    slideChoice(uiChoice(*canvas.state, toolLeft, {
        .labels = spaceLabels,
        .count = 2,
        .selected = hud->space,
        .width = spaceW,
        .onPick = onSpace,
        .user = hud,
    }), spaceW);
    const char* pivotLabels[] = {"Pivot", "Center"};
    const float pivotW = fitChoice(pivotLabels, 2);
    slideChoice(uiChoice(*canvas.state, toolLeft, {
        .labels = pivotLabels,
        .count = 2,
        .selected = hud->pivotSpace,
        .width = pivotW,
        .onPick = onPivotSpace,
        .user = hud,
    }), pivotW);
    (void)toolSep(toolLeft);
    const float moveSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    const float rotSteps[] = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    const float scaleSteps[] = {0.1f, 0.25f, 0.5f, 1.0f};
    const char* moveNames[] = {"0.1", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0"};
    const char* rotNames[] = {"5", "10", "15", "30", "45", "90"};
    const char* scaleNames[] = {"0.1", "0.25", "0.5", "1.0"};
    const float* stepTable[] = {moveSteps, rotSteps, scaleSteps};
    const char* const* nameTable[] = {moveNames, rotNames, scaleNames};
    const uint8_t stepCount[] = {7, 6, 4};
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const uint8_t snapIcons[3] = {3, 4, 5};
        uint16_t toggle = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f, .iconSlot = snapIcons[channel] });
        hud->snapBtn[channel] = toggle;
        hud->snapIcon[channel] = iconChild(*canvas.state, toggle);
        if (toggle < canvas.state->count) {
            if (auto* paint = canvas.state->ent[toggle].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Custom;
                paint->r = 1.0f;
                paint->g = 1.0f;
                paint->b = 1.0f;
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
            }
            if (auto* flex = canvas.state->ent[toggle].try_get_mut<UiFlex>()) {
                flex->pad = 0.0f;
                flex->padL = 0.0f;
                flex->padR = 0.0f;
                flex->padT = 0.0f;
                flex->padB = 0.0f;
                flex->align = UiAlign::Center;
                flex->justify = UiJustify::Center;
            }
        }
        if (hud->snapIcon[channel] < canvas.state->count) {
            if (auto* flex = canvas.state->ent[hud->snapIcon[channel]].try_get_mut<UiFlex>()) {
                flex->width = 28.0f;
                flex->height = 28.0f;
            }
        }
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.hud = hud;
        gizmo.mode = channel;
        uiBind(*canvas.state, toggle, onSnapToggle, &gizmo);
        const uint8_t picked = hud->snapPick[channel] < stepCount[channel] ? hud->snapPick[channel] : 0;
        hud->snapSent[channel] = stepTable[channel][picked];
        const uint16_t num = uiNumber(*canvas.state, toolLeft, hud->snapSent[channel], 0.001f, 10000.0f, channel == 1 ? 1.0f : 0.01f);
        hud->snapNum[channel] = num;
        uiNumberFill(*canvas.state, num, false);
        uiSetHeight(*canvas.state, num, 28.0f);
        if (num < canvas.state->count) {
            if (auto* flex = canvas.state->ent[num].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 72.0f;
                flex->grow = 0.0f;
                flex->shrink = 0.0f;
            }
        }
        uint16_t menu = uiMakeMenu(*canvas.state);
        if (menu != kUiNone) {
            canvas.state->ent[menu].add<UiMenuSlide>();
            if (auto* flex = canvas.state->ent[menu].try_get_mut<UiFlex>()) {
                flex->width = 108.0f;
            }
        }
        uint16_t arrow = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f });
        if (arrow < canvas.state->count) {
            if (auto* paint = canvas.state->ent[arrow].try_get_mut<UiPaint>()) {
                paint->icon = 11;
            }
            uiSetContextMenu(*canvas.state, arrow, menu);
            uiMarkMenuBar(*canvas.state, arrow);
        }
        for (uint8_t step = 0; step < stepCount[channel]; ++step) {
            hud->snaps.push_back({});
            SnapHit& pick = hud->snaps.back();
            pick.hud = hud;
            pick.channel = channel;
            pick.index = step;
            pick.step = stepTable[channel][step];
            if (menu != kUiNone) {
                (void)uiCommandAddTo(*canvas.state, menu, nameTable[channel][step], onSnapPick, &pick);
            }
        }
    }
    (void)toolSep(toolLeft);
    {
        auto transport = [&](uint16_t id) {
            if (id >= canvas.state->count) {
                return;
            }
            if (auto* flex = canvas.state->ent[id].try_get_mut<UiFlex>()) {
                flex->pad = 0.0f;
                flex->padL = 0.0f;
                flex->padR = 0.0f;
                flex->padT = 0.0f;
                flex->padB = 0.0f;
                flex->align = UiAlign::Center;
                flex->justify = UiJustify::Center;
            }
            if (auto* paint = canvas.state->ent[id].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Custom;
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
                paint->radius = 0.0f;
            }
        };
        uint16_t play = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f });
        hud->playBtn = play;
        transport(play);
        UiFlex mark{};
        mark.widthMode = UiSize::Px;
        mark.width = 28.0f;
        mark.heightMode = UiSize::Px;
        mark.height = 28.0f;
        mark.shrink = 0.0f;
        UiPaint glyph = paintClear();
        glyph.icon = 17;
        glyph.r = 1.0f;
        glyph.g = 1.0f;
        glyph.b = 1.0f;
        hud->playGlyph = uiPanel(*canvas.state, play, mark, glyph);
        uiBind(*canvas.state, play, onPlay, hud);
        uint16_t pause = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f });
        hud->pauseBtn = pause;
        transport(pause);
        glyph.icon = 18;
        (void)uiPanel(*canvas.state, pause, mark, glyph);
        uiBind(*canvas.state, pause, onPause, hud);
        uiShow(*canvas.state, pause, false);
    }
    paintTools(*hud);
    uint16_t camBtn = uiButton(*canvas.state, toolRight, { .label = "", .width = 52.0f, .height = 28.0f, .iconSlot = 6 });
    hud->camBtn = camBtn;
    if (camBtn < canvas.state->count) {
        if (auto* flex = canvas.state->ent[camBtn].try_get_mut<UiFlex>()) {
            flex->direction = UiFlexDir::Row;
            flex->align = UiAlign::Center;
            flex->justify = UiJustify::Center;
            flex->gap = 2.0f;
        }
        UiFlex mark{};
        mark.widthMode = UiSize::Px;
        mark.width = 14.0f;
        mark.heightMode = UiSize::Px;
        mark.height = 14.0f;
        mark.shrink = 0.0f;
        UiPaint glyph = paintClear();
        glyph.icon = 11;
        glyph.r = look.text[0];
        glyph.g = look.text[1];
        glyph.b = look.text[2];
        (void)uiPanel(*canvas.state, camBtn, mark, glyph);
    }
    tintIcon(*canvas.state, iconChild(*canvas.state, camBtn), false);
    uiBind(*canvas.state, camBtn, onCameraButton, hud);
    uint16_t viewBtn = uiMenuButton(*canvas.state, toolRight, "Lit", kUiNone);
    hud->viewBtn = viewBtn;
    uiBind(*canvas.state, viewBtn, onViewButton, hud);
    uint16_t viewCap = kUiNone;
    for (uint16_t child = 0; child < canvas.state->count; ++child) {
        if (canvas.state->parentOf[child] == viewBtn && canvas.state->ent[child].try_get<UiText>() != nullptr) {
            viewCap = child;
            break;
        }
    }
    hud->viewCap = viewCap;
    float viewW = uiMeasure(*canvas.state, "Lit", 3, 3) + 38.0f;
    for (const ViewMode& mode : kViews) {
        uint8_t len = 0;
        while (mode.name[len] != '\0' && len < 48) {
            ++len;
        }
        const float measured = uiMeasure(*canvas.state, mode.name, len, len);
        if (measured + 52.0f > viewW) {
            viewW = measured + 52.0f;
        }
    }
    if (viewBtn < canvas.state->count) {
        if (auto* flex = canvas.state->ent[viewBtn].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = uiMeasure(*canvas.state, "Lit", 3, 3) + 38.0f;
            flex->grow = 0.0f;
            flex->shrink = 0.0f;
        }
    }
    if (viewCap != kUiNone) {
        uiTextStyle(*canvas.state, viewCap, UiTextAlign::Left, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    }
    const float viewH = 16.0f + static_cast<float>(sizeof(kViews) / sizeof(kViews[0])) * 36.0f
        + static_cast<float>(sizeof(kViews) / sizeof(kViews[0]) - 1) * 2.0f;
    hud->viewPopH = viewH;
    UiFlex viewPopFlex{};
    viewPopFlex.position = UiPosition::Absolute;
    viewPopFlex.direction = UiFlexDir::Column;
    viewPopFlex.widthMode = UiSize::Px;
    viewPopFlex.width = viewW;
    viewPopFlex.heightMode = UiSize::Px;
    viewPopFlex.height = viewH;
    viewPopFlex.pad = 8.0f;
    viewPopFlex.gap = 2.0f;
    viewPopFlex.shrink = 0.0f;
    const uint16_t viewPop = uiPanel(*canvas.state, root, viewPopFlex, veilPaint());
    hud->viewPanel = viewPop;
    canvas.state->ent[viewPop].add<UiMenuSlide>();
    uiShow(*canvas.state, viewPop, false);
    for (const ViewMode& mode : kViews) {
        hud->modes.push_back({});
        ModeHit& hit = hud->modes.back();
        hit.canvas = &canvas;
        hit.hud = hud;
        hit.index = mode.index;
        hit.label = viewCap;
        const uint16_t radio = uiCheck(*canvas.state, viewPop, mode.name, 1, 12, mode.index == 0);
        hit.radio = radio;
        if (radio < canvas.state->count) {
            if (auto* paint = canvas.state->ent[radio].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
            }
            uiBind(*canvas.state, radio, onViewRadio, &hit);
        }
    }
    {
        uint16_t gear = uiButton(*canvas.state, toolRight, { .label = "", .width = 28.0f, .height = 28.0f });
        hud->gearBtn = gear;
        if (gear < canvas.state->count) {
            if (auto* paint = canvas.state->ent[gear].try_get_mut<UiPaint>()) {
                paint->icon = 20;
            }
        }
        uiBind(*canvas.state, gear, onGear, hud);
        UiFlex pop{};
        pop.position = UiPosition::Absolute;
        pop.direction = UiFlexDir::Column;
        pop.widthMode = UiSize::Px;
        pop.width = 280.0f;
        pop.heightMode = UiSize::Px;
        pop.height = 252.0f;
        pop.pad = 8.0f;
        pop.gap = 4.0f;
        pop.shrink = 0.0f;
        const uint16_t statsPop = uiPanel(*canvas.state, root, pop, veilPaint());
        hud->statsPanel = statsPop;
        canvas.state->ent[statsPop].add<UiMenuSlide>();
        uiShow(*canvas.state, statsPop, false);
        const char* statNames[] = {
            "Show FPS / Frame Time",
            "Show GPU Time",
            "Show Draw Calls / Triangle Count",
            "Show Physics Time (Jolt)",
            "Show Memory",
            "Show VRAM Memory",
        };
        for (uint8_t i = 0; i < 6; ++i) {
            hud->statCheck[i] = uiCheck(*canvas.state, statsPop, statNames[i], 0, 0, false);
        }
    }
    UiFlex camFlex{};
    camFlex.position = UiPosition::Absolute;
    camFlex.direction = UiFlexDir::Column;
    camFlex.widthMode = UiSize::Px;
    camFlex.width = 400.0f;
    camFlex.heightMode = UiSize::Px;
    camFlex.height = 500.0f;
    camFlex.pad = 8.0f;
    camFlex.gap = 6.0f;
    camFlex.shrink = 0.0f;
    const uint16_t camPanel = uiPanel(*canvas.state, root, camFlex, veilPaint());
    hud->camPanel = camPanel;
    canvas.state->ent[camPanel].add<UiMenuSlide>();
    uiShow(*canvas.state, camPanel, false);
    auto rule = [&]() {
        UiFlex line{};
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.heightMode = UiSize::Px;
        line.height = 1.0f;
        line.shrink = 0.0f;
        UiPaint ink = paintToken(look, UiToken::Muted, 0.0f);
        ink.a = 0.45f;
        return uiPanel(*canvas.state, camPanel, line, ink);
    };
    const char* freeViews[] = {"Perspective", "Orthographic"};
    const char* axisViews[] = {"Top", "Bottom", "Left", "Right", "Front", "Back"};
    for (uint8_t i = 0; i < 2; ++i) {
        hud->camProj[i] = uiCheck(*canvas.state, camPanel, freeViews[i], 1, 9, i == 0);
    }
    (void)rule();
    for (uint8_t i = 0; i < 6; ++i) {
        hud->camAxis[i] = uiCheck(*canvas.state, camPanel, axisViews[i], 1, 10, false);
    }
    for (uint16_t id : hud->camProj) {
        if (id < canvas.state->count) {
            if (auto* paint = canvas.state->ent[id].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
            }
        }
    }
    for (uint16_t id : hud->camAxis) {
        if (id < canvas.state->count) {
            if (auto* paint = canvas.state->ent[id].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
            }
        }
    }
    auto labeledNumber = [&](const char* text, float value, float minV, float maxV, float step) {
        UiFlex line{};
        line.direction = UiFlexDir::Row;
        line.align = UiAlign::Center;
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.heightMode = UiSize::Px;
        line.height = 32.0f;
        line.gap = 8.0f;
        line.shrink = 0.0f;
        const uint16_t host = uiPanel(*canvas.state, camPanel, line, hiddenPaint());
        (void)uiLabel(*canvas.state, host, text);
        UiFlex gap{};
        gap.grow = 1.0f;
        gap.shrink = 1.0f;
        gap.widthMode = UiSize::Px;
        gap.width = 8.0f;
        gap.heightMode = UiSize::Px;
        gap.height = 1.0f;
        (void)uiSpacer(*canvas.state, host, gap);
        const uint16_t num = uiNumber(*canvas.state, host, value, minV, maxV, step);
        uiSetHeight(*canvas.state, num, 28.0f);
        if (num < canvas.state->count) {
            if (auto* flex = canvas.state->ent[num].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 72.0f;
                flex->grow = 0.0f;
                flex->shrink = 0.0f;
            }
        }
        return num;
    };
    (void)rule();
    {
        UiFlex line{};
        line.direction = UiFlexDir::Row;
        line.align = UiAlign::Center;
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.heightMode = UiSize::Px;
        line.height = 32.0f;
        line.gap = 8.0f;
        line.shrink = 0.0f;
        const uint16_t host = uiPanel(*canvas.state, camPanel, line, hiddenPaint());
        (void)uiLabel(*canvas.state, host, "Field of View");
        hud->camFov = uiSlider(*canvas.state, host, 60.0f, 10.0f, 120.0f, nullptr, nullptr, 32.0f);
        if (hud->camFov < canvas.state->count) {
            if (auto* flex = canvas.state->ent[hud->camFov].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 32.0f;
                flex->grow = 1.0f;
                flex->shrink = 1.0f;
                flex->minW = 24.0f;
            }
        }
        hud->camFovNum = uiNumber(*canvas.state, host, 60.0f, 10.0f, 120.0f, 1.0f);
        uiSetHeight(*canvas.state, hud->camFovNum, 28.0f);
        if (hud->camFovNum < canvas.state->count) {
            if (auto* flex = canvas.state->ent[hud->camFovNum].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 72.0f;
                flex->grow = 0.0f;
                flex->shrink = 0.0f;
            }
        }
    }
    (void)rule();
    hud->camNear = labeledNumber("Near View Plane", 0.1f, 0.01f, 10.0f, 0.01f);
    hud->camFar = labeledNumber("Far View Plane", 8000.0f, 100.0f, 100000.0f, 10.0f);
    (void)rule();
    hud->camSpeed = labeledNumber("Camera Speed", 4.0f, 0.05f, 200.0f, 0.1f);
    UiFlex viewFlex{};
    viewFlex.grow = 1.0f;
    viewFlex.shrink = 1.0f;
    viewFlex.minH = 0.0f;
    const uint16_t viewPort = uiPanel(*canvas.state, viewCol, viewFlex, hiddenPaint());
    UiFlex over{};
    over.position = UiPosition::Absolute;
    over.direction = UiFlexDir::Column;
    over.widthMode = UiSize::Px;
    over.width = 240.0f;
    over.heightMode = UiSize::Auto;
    over.pad = 8.0f;
    over.gap = 2.0f;
    over.shrink = 0.0f;
    over.posX = 8.0f;
    over.posY = 8.0f;
    UiPaint veil = paintToken(look, UiToken::Content, look.radiusSm);
    veil.a = 0.72f;
    hud->overlay = uiPanel(*canvas.state, viewPort, over, veil);
    if (hud->overlay < canvas.state->count) {
        canvas.state->ent[hud->overlay].add<UiPass>();
    }
    uiShow(*canvas.state, hud->overlay, false);
    for (uint8_t i = 0; i < 6; ++i) {
        const uint16_t line = uiLabel(*canvas.state, hud->overlay, "");
        hud->statLine[i] = line;
        if (line < canvas.state->count) {
            if (auto* paint = canvas.state->ent[line].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Custom;
                paint->r = look.accent[0][0];
                paint->g = look.accent[0][1];
                paint->b = look.accent[0][2];
                paint->a = 0.0f;
            }
        }
        uiShow(*canvas.state, line, false);
    }
    (void)splitBar(row, true);
    UiFlex dockFlex{};
    dockFlex.direction = UiFlexDir::Row;
    dockFlex.widthMode = UiSize::Px;
    dockFlex.width = 300.0f;
    dockFlex.shrink = 0.0f;
    dockFlex.heightMode = UiSize::Percent;
    dockFlex.height = 100.0f;
    dockFlex.minH = 72.0f;
    const uint16_t dock = uiPanel(*canvas.state, row, dockFlex, hiddenPaint());
    UiFlex statsFlex{};
    statsFlex.direction = UiFlexDir::Column;
    statsFlex.widthMode = UiSize::Px;
    statsFlex.width = 420.0f;
    statsFlex.shrink = 0.0f;
    statsFlex.minW = 360.0f;
    const uint16_t statsPane = uiPanel(*canvas.state, dock, statsFlex, dockPaint());
    uiShow(*canvas.state, statsPane, false);
    UiFlex statsCol{};
    statsCol.gap = 4.0f;
    statsCol.pad = 8.0f;
    statsCol.shrink = 0.0f;
    const uint16_t stats = uiPanel(*canvas.state, statsPane, statsCol, hiddenPaint());
    hud->stats.canvas = &canvas;
    hud->stats.page = statsPane;
    hud->stats.dock = dock;
    hud->stats.open = 0;
    UiFlex sideFlex{};
    sideFlex.direction = UiFlexDir::Column;
    sideFlex.grow = 1.0f;
    sideFlex.shrink = 1.0f;
    sideFlex.minW = 180.0f;
    sideFlex.minH = 72.0f;
    const uint16_t side = uiPanel(*canvas.state, dock, sideFlex, hiddenPaint());
    UiFlex hierFlex{};
    hierFlex.grow = 1.0f;
    hierFlex.shrink = 1.0f;
    hierFlex.minH = 40.0f;
    UiPaint hierScroll{};
    hierScroll.r = look.content[0];
    hierScroll.g = look.content[1];
    hierScroll.b = look.content[2];
    hierScroll.a = 1.0f;
    const uint16_t hierView = uiPanel(*canvas.state, side, hierFlex, hierScroll);
    UiFlex hierCol{};
    hierCol.gap = 4.0f;
    hierCol.pad = 8.0f;
    hierCol.shrink = 0.0f;
    hierCol.widthMode = UiSize::Percent;
    hierCol.width = 100.0f;
    const uint16_t objects = uiPanel(*canvas.state, hierView, hierCol, hiddenPaint());
    if (canvas.state != nullptr) {
        uiBindScroll(*canvas.state, hierView, objects);
    }
    const uint16_t hierarchy = uiLabel(*canvas.state, objects, "Hierarchy");
    if (auto* paint = canvas.state->ent[hierarchy].try_get_mut<UiPaint>()) {
        paint->token = UiToken::Custom;
        paint->r = look.accent[0][0];
        paint->g = look.accent[0][1];
        paint->b = look.accent[0][2];
        paint->a = 0.0f;
    }
    hud->tool.objects = objects;
    (void)splitBar(side, false);
    UiFlex inspFlex{};
    inspFlex.direction = UiFlexDir::Column;
    inspFlex.overflow = UiOverflow::Hidden;
    inspFlex.heightMode = UiSize::Px;
    inspFlex.height = 420.0f;
    inspFlex.minH = 40.0f;
    inspFlex.shrink = 1.0f;
    const uint16_t insp = uiPanel(*canvas.state, side, inspFlex, hiddenPaint());
    (void)uiPropFilterBar(*canvas.state, insp);
    const uint16_t detailsInner = scrollPage(insp, true);
    uint16_t details = uiPropHost(*canvas.state, detailsInner);
    const uint16_t detailsView = uiParent(*canvas.state, detailsInner);
    hud->tool.postPage = detailsView;
    hud->tool.sunPage = detailsView;
    hud->tool.propPage = detailsView;
    if (canvas.state != nullptr) {
        uint16_t cubeBtn = uiButton(*canvas.state, objects, { .label = "Куб", .fillWidth = true });
        uiSetHeight(*canvas.state, cubeBtn, 28.0f);
        if (cubeBtn != kUiNone) {
            uiBind(*canvas.state, cubeBtn, onSpawnCube, hud);
        }
    }
    if (hud->tool.scene != nullptr) {
        const uint8_t count = sceneFrameActorCount(hud->tool.scene);
        for (uint8_t i = 0; i < count; ++i) {
            char name[24];
            uint8_t kind = 0;
            sceneFrameActorName(hud->tool.scene, i, name, sizeof(name), &kind);
            (void)addActorButton(*hud, objects, i, name, kind);
        }
    }
    (void)splitBar(root, false);
    UiFlex fileFlex{};
    fileFlex.direction = UiFlexDir::Column;
    fileFlex.heightMode = UiSize::Px;
    fileFlex.height = 200.0f;
    fileFlex.minH = 120.0f;
    fileFlex.shrink = 1.0f;
    const uint16_t files = uiPanel(*canvas.state, root, fileFlex, dockPaint());
    filesShutdown(*canvas.state);
    uiFiles(*canvas.state).fileFrame = files;
    filesAttach(*canvas.state);
    filesMount(*canvas.state);
    filesShow(*canvas.state, true);
    UiFlex modeFlex{};
    modeFlex.shrink = 0.0f;
    const uint16_t modeHost = uiPanel(*canvas.state, root, modeFlex, hiddenPaint());
    uiShow(*canvas.state, modeHost, false);
    uint16_t modes = modeHost;
    uint16_t panel = stats;
    uiName(*canvas.state, uiLabel(*canvas.state, panel, "fps"), "stats");
    uiName(*canvas.state, uiLabel(*canvas.state, panel, "mem"), "mem");
    uiName(*canvas.state, uiLabel(*canvas.state, panel, "gpu"), "load");
    auto tick = [&](const char* label, const char* id, bool on) {
        uint16_t box = uiPropCheck(*canvas.state, panel, label, 0, 0, on);
        uiName(*canvas.state, box, id);
    };
    auto hideCheck = [&](const char* label, const char* id, uint8_t kind, uint8_t group, bool on) {
        uint16_t box = uiCheck(*canvas.state, modeHost, label, kind, group, on);
        uiName(*canvas.state, box, id);
        uiShow(*canvas.state, box, false);
    };
    auto hideSlide = [&](const char* id, float value, float minV, float maxV) {
        const uint16_t num = uiNumber(*canvas.state, modeHost, value, minV, maxV, 0.01f);
        uiName(*canvas.state, num, id);
        uiShow(*canvas.state, num, false);
    };
    auto group = [&](uint16_t parent, const char* name) {
        UiFlex col{};
        col.direction = UiFlexDir::Column;
        col.shrink = 0.0f;
        col.widthMode = UiSize::Percent;
        col.width = 100.0f;
        const uint16_t id = uiPanel(*canvas.state, parent, col, hiddenPaint());
        if (name != nullptr) {
            uiName(*canvas.state, id, name);
        }
        return id;
    };
    auto vec = [&](uint16_t host, const char* label, const char* nx, const char* ny, const char* nz, float x, float y, float z, float minV, float maxV, float step, int slot) {
        const UiPropVec3 row = uiPropVec3(*canvas.state, host, label, x, y, z, minV, maxV, step);
        uiName(*canvas.state, row.x, nx);
        uiName(*canvas.state, row.y, ny);
        uiName(*canvas.state, row.z, nz);
        if (slot >= 0 && slot < 4) {
            hud->xformLock[slot] = row.lock;
        }
    };
    const uint8_t catGeneral = static_cast<uint8_t>(UiPropCat::General);
    const uint8_t catActor = static_cast<uint8_t>(UiPropCat::Actor);
    const uint8_t catLod = static_cast<uint8_t>(UiPropCat::Lod);
    const uint8_t catMisc = static_cast<uint8_t>(UiPropCat::Misc);
    const uint8_t catRender = static_cast<uint8_t>(UiPropCat::Rendering);
    const uint8_t catStream = static_cast<uint8_t>(UiPropCat::Streaming);
    panel = uiPropSection(*canvas.state, details, "Transform", catGeneral, kSecTransform);
    constexpr float kOpen = 3.4028234e+38f;
    vec(panel, "Location", "pbrPx", "pbrPy", "pbrPz", 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f, 0);
    vec(panel, "Rotation", "pbrEx", "pbrEy", "pbrEz", 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.5f, 1);
    vec(panel, "Scale", "pbrSx", "pbrSy", "pbrSz", 1.0f, 1.0f, 1.0f, -kOpen, kOpen, 0.05f, 2);
    vec(panel, "Pivot", "pbrOx", "pbrOy", "pbrOz", 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f, 3);
    tick("Static", "xfStatic", false);
    panel = uiPropSection(*canvas.state, details, "Material", catActor, kSecMaterial);
    rowSlide(*canvas.state, panel, "Color R", "pbrR", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Color G", "pbrG", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Color B", "pbrB", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Roughness", "pbrRough", 0.5f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Metallic", "pbrMetal", 0.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Emissive", "pbrEm", 0.0f, 0.0f, 8.0f);
    const uint16_t postBody = uiPropSection(*canvas.state, details, "PostProcessing", catRender, kSecPost);
    const uint16_t giBody = uiFoldBody(*canvas.state, postBody, "GI");
    const uint16_t directBody = uiFoldBody(*canvas.state, giBody, "Direct Illumination");
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "None", diOff, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Restir", diRestir, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, directBody, "Mode", "Restir", menu), "diBtn");
    }
    hideCheck("ReSTIR", "restiren", 0, 0, true);
    panel = group(directBody, "diBody");
    tick("Temporal", "rsTOn", true);
    tick("Spatial", "rsSOn", true);
    tick("Boil", "rsBoilOn", true);
    tick("Visibility", "rsVis", false);
    tick("Permutation", "rsPerm", true);
    tick("Shortcut", "rsShort", false);
    tick("Material", "rsMat", true);
    tick("Naive", "rsNaive", true);
    tick("Sun ray", "rsSunOn", true);
    rowSlide(*canvas.state, panel, "Candidates", "rsLocal", 8.0f, 0.0f, 16.0f);
    rowSlide(*canvas.state, panel, "History", "rsHistory", 20.0f, 1.0f, 40.0f);
    rowSlide(*canvas.state, panel, "Temporal depth", "rsTDepth", 0.1f, 0.01f, 0.5f);
    rowSlide(*canvas.state, panel, "Temporal normal", "rsTNorm", 0.9f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Temporal ray", "rsTBias", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "Permute threshold", "rsPermTh", 0.9f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Boil", "rsBoil", 0.2f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Neighbors", "rsSpatial", 3.0f, 0.0f, 8.0f);
    rowSlide(*canvas.state, panel, "Radius", "rsRadius", 4.0f, 1.0f, 32.0f);
    rowSlide(*canvas.state, panel, "Spatial depth", "rsSDepth", 0.1f, 0.01f, 0.5f);
    rowSlide(*canvas.state, panel, "Spatial normal", "rsSNorm", 0.9f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Spatial ray", "rsSBias", 3.0f, 0.0f, 3.0f);
    rowSlide(*canvas.state, panel, "Boost", "rsSBoost", 3.0f, 0.0f, 16.0f);
    rowSlide(*canvas.state, panel, "Target", "rsTarget", 20.0f, 1.0f, 40.0f);
    rowSlide(*canvas.state, panel, "Sun disk", "rsSun", 0.5f, 0.0f, 4.0f);
    rowSlide(*canvas.state, panel, "Sun rays", "rsSunRays", 1.0f, 1.0f, 8.0f);
    rowSlide(*canvas.state, panel, "Shadow rays", "rsShadowRays", 1.0f, 1.0f, 8.0f);
    rowSlide(*canvas.state, panel, "Shadow range m", "rsShadowRange", 0.0f, 0.0f, 200.0f);
    rowSlide(*canvas.state, panel, "Shadow bias m", "rsShadowBias", 0.05f, 0.02f, 2.0f);
    const uint16_t indirectBody = uiFoldBody(*canvas.state, giBody, "Indirect Illumination");
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "None", giOff, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Sharc", giRs, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, indirectBody, "Mode", "Sharc", menu), "giBtn");
    }
    hideCheck("Off", "prof0", 1, 3, false);
    hideCheck("VXGI", "prof1", 1, 3, false);
    hideCheck("Cascades", "prof2", 1, 3, false);
    hideCheck("SHARC", "prof3", 1, 3, true);
    hideCheck("GI", "gien", 0, 0, true);
    hideCheck("probes", "probes", 0, 0, true);
    hideCheck("radiance", "rcen", 0, 0, false);
    hideSlide("rcRays", 1.0f, 1.0f, 16.0f);
    hideSlide("rcCell", 0.8f, 0.1f, 4.0f);
    hideSlide("rcGain", 1.0f, 0.0f, 4.0f);
    hideSlide("rcMax", 48.0f, 0.5f, 200.0f);
    hideSlide("rcSun", 1.0f, 0.0f, 2.0f);
    hideSlide("rcBounce", 1.0f, 0.0f, 2.0f);
    panel = group(indirectBody, "indBody");
    tick("Denoise", "denoise", true);
    rowSlide(*canvas.state, panel, "GI brightness", "giGain", 2.0f, 0.0f, 6.0f);
    const uint16_t reflectFold = uiFoldBody(*canvas.state, giBody, "Reflection");
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "None", reflOff, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "SSSR", reflSssr, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "SSSR + RT", reflRt, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, reflectFold, "Mode", "SSSR + RT", menu), "reflBtn");
    }
    hideCheck("reflect", "reflecten", 0, 0, true);
    hideCheck("rt", "reflRt", 0, 0, true);
    panel = group(reflectFold, "reflBody");
    rowSlide(*canvas.state, panel, "Roughness", "ssrCut", 0.82f, 0.05f, 1.0f);
    rowSlide(*canvas.state, panel, "Steps", "ssrSteps", 64.0f, 16.0f, 96.0f);
    rowSlide(*canvas.state, panel, "Thickness", "ssrThick", 0.02f, 0.02f, 2.0f);
    rowSlide(*canvas.state, panel, "Distance", "ssrMax", 80.0f, 4.0f, 200.0f);
    rowSlide(*canvas.state, panel, "Blur", "ssrBlur", 1.0f, 0.0f, 3.0f);
    rowSlide(*canvas.state, panel, "Metal", "ssrMetal", 0.35f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Edge px", "ssrEdge", 24.0f, 0.0f, 64.0f);
    panel = group(reflectFold, "rtBody");
    rowSlide(*canvas.state, panel, "Ray roughness", "ssrRt", 0.45f, 0.0f, 1.0f);
    const uint16_t effects = uiFoldBody(*canvas.state, postBody, "Effects");
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "None", aoOff, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "XeGTAO", aoGtao, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, effects, "Ambient Occlusion", "XeGTAO", menu), "aoBtn");
    }
    hideCheck("GTAO", "gtaoen", 0, 0, true);
    panel = group(effects, "aoBody");
    rowSlide(*canvas.state, panel, "Radius", "gtaor", 0.5f, 0.05f, 4.0f);
    rowSlide(*canvas.state, panel, "Slices", "gtaoSl", 3.0f, 1.0f, 9.0f);
    rowSlide(*canvas.state, panel, "Steps", "gtaoSt", 3.0f, 1.0f, 4.0f);
    rowSlide(*canvas.state, panel, "Power", "gtaoPw", 2.2f, 0.5f, 4.0f);
    rowSlide(*canvas.state, panel, "Falloff", "gtaoFa", 0.615f, 0.05f, 1.0f);
    rowSlide(*canvas.state, panel, "Denoise", "gtaoDn", 1.0f, 0.0f, 3.0f);
    rowSlide(*canvas.state, panel, "Strength", "gtao", 1.0f, 0.0f, 1.0f);
    const uint16_t sscsFold = uiFoldBody(*canvas.state, effects, "SSCS");
    panel = sscsFold;
    tick("Enabled", "contacten", false);
    panel = group(sscsFold, "sscsBody");
    rowSlide(*canvas.state, panel, "Strength", "contact", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Radius m", "sscsR", 1.0f, 0.05f, 8.0f);
    rowSlide(*canvas.state, panel, "Steps", "sscsS", 8.0f, 1.0f, 32.0f);
    rowSlide(*canvas.state, panel, "Thickness", "sscsT", 0.02f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Pixels", "sscsP", 128.0f, 1.0f, 256.0f);
    rowSlide(*canvas.state, panel, "Spec occlusion", "spec", 1.0f, 0.0f, 1.0f);
    const uint16_t bloomFold = uiFoldBody(*canvas.state, effects, "Bloom");
    panel = bloomFold;
    tick("Enabled", "bloomen", true);
    panel = group(bloomFold, "bloomBody");
    rowSlide(*canvas.state, panel, "Strength", "bloom", 0.15f, 0.0f, 0.80f);
    rowSlide(*canvas.state, panel, "Threshold", "bloomth", 0.35f, 0.0f, 2.0f);
    const uint16_t lens = uiFoldBody(*canvas.state, effects, "Lens");
    panel = lens;
    rowSlide(*canvas.state, panel, "Focus m", "dofFocus", 8.0f, 0.2f, 80.0f);
    rowSlide(*canvas.state, panel, "Bokeh", "dofAmount", 0.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Flare", "flare", 0.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Grain", "grain", 0.0f, 0.0f, 0.12f);
    rowSlide(*canvas.state, panel, "Vignette", "vig", 0.35f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "Chromatic px", "ca", 2.0f, 0.0f, 4.0f);
    const uint16_t toneBody = uiFoldBody(*canvas.state, postBody, "Tonemapping");
    panel = toneBody;
    hideCheck("AgX", "tone0", 1, 2, true);
    hideCheck("ACES", "tone1", 1, 2, false);
    hideCheck("Filmic", "tone2", 1, 2, false);
    hideCheck("Linear", "tone3", 1, 2, false);
    hideCheck("PBR Neutral", "tone4", 1, 2, false);
    hideCheck("GT7", "tone5", 1, 2, false);
    hideCheck("Generic", "tone6", 1, 2, false);
    hideCheck("Reinhard", "tone7", 1, 2, false);
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "AgX", toneAgx, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "ACES", toneAces, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Filmic", toneFilmic, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Linear", toneLinear, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "PBR Neutral", toneNeutral, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "GT7", toneGt7, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Generic", toneGeneric, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Reinhard", toneReinhard, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, panel, "Operator", "AgX", menu), "toneBtn");
    }
    tick("Luminance", "lumScale", false);
    tick("Gamut", "gamut", false);
    rowSlide(*canvas.state, panel, "Brightness", "bright", 1.0f, 0.05f, 4.0f);
    rowSlide(*canvas.state, panel, "Exposure EV", "exposure", -1.0f, -8.0f, 8.0f);
    rowSlide(*canvas.state, panel, "Temperature", "temperature", 0.0f, -1.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Tint", "tint", 0.0f, -1.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Night", "night", 0.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Contrast", "contrast", 1.0f, 0.5f, 1.5f);
    rowSlide(*canvas.state, panel, "Saturation", "saturation", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "Mix", "mix", 0.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Vibrance", "vibrance", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "Generic contrast", "genContrast", 1.0f, 0.5f, 2.0f);
    rowSlide(*canvas.state, panel, "Generic mid in", "midIn", 0.18f, 0.01f, 1.0f);
    rowSlide(*canvas.state, panel, "Generic mid out", "midOut", 0.18f, 0.01f, 1.0f);
    rowSlide(*canvas.state, panel, "Generic HDR", "hdrMax", 10.0f, 1.0f, 64.0f);
    rowSlide(*canvas.state, panel, "Shadows", "shadows", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "Midtones", "midtones", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "Highlights", "highlights", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "CDL slope", "slope", 1.0f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "CDL offset", "cdlOffset", 0.0f, -0.5f, 0.5f);
    rowSlide(*canvas.state, panel, "CDL power", "power", 1.0f, 0.2f, 2.0f);
    const uint16_t finalBody = uiFoldBody(*canvas.state, postBody, "Final");
    panel = finalBody;
    hideCheck("SMAA", "smaa", 0, 0, false);
    hideCheck("CMAA", "cmaa", 0, 0, false);
    hideCheck("FSR", "fsr", 0, 0, true);
    hideCheck("FXAA", "fxaa", 0, 0, false);
    hideCheck("TAA", "taa", 0, 0, false);
    hideCheck("Native", "fsrq0", 1, 4, true);
    hideCheck("Quality", "fsrq1", 1, 4, false);
    hideCheck("Balanced", "fsrq2", 1, 4, false);
    hideCheck("Performance", "fsrq3", 1, 4, false);
    hideCheck("Ultra", "fsrq4", 1, 4, false);
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "Off", aaOff, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "SMAA", aaSmaa, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "CMAA", aaCmaa, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "FSR", aaFsr, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "TAA", aaTaa, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "FXAA", aaFxaa, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, panel, "Anti-aliasing", "TAA", menu), "aaBtn");
    }
    panel = group(finalBody, "fsrBody");
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "Native", fsrNative, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Quality", fsrQuality, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Balanced", fsrBalanced, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Performance", fsrPerformance, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Ultra", fsrUltra, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, panel, "FSR scale", "Native", menu), "fsrBtn");
    }
    rowSlide(*canvas.state, panel, "Sharpness", "fsrSharp", 0.2f, 0.0f, 1.0f);
    const uint16_t atmo = uiPropSection(*canvas.state, details, "Atmosphere", catRender, kSecAtmo);
    const uint16_t fogFold = uiFoldBody(*canvas.state, atmo, "Fog");
    panel = fogFold;
    tick("Enabled", "fogen", false);
    panel = group(fogFold, "fogBody");
    rowSlide(*canvas.state, panel, "Fog Density", "fog", 0.06f, 0.0f, 0.5f);
    rowSlide(*canvas.state, panel, "Fog Falloff", "fogFall", 0.08f, 0.0f, 1.5f);
    {
        const uint16_t swatch = uiPropSwatch(*canvas.state, panel, "Fog Color", 0.62f, 0.72f, 0.86f);
        uiName(*canvas.state, swatch, "fogSwatch");
        uiBind(*canvas.state, swatch, onFogSwatch, hud);
    }
    hideSlide("fogR", 0.62f, 0.0f, 1.0f);
    hideSlide("fogG", 0.72f, 0.0f, 1.0f);
    hideSlide("fogB", 0.86f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Fog Height", "fogH", 0.0f, -40.0f, 40.0f);
    rowSlide(*canvas.state, panel, "Fog Start", "fogStart", 0.0f, 0.0f, 80.0f);
    rowSlide(*canvas.state, panel, "Fog Opacity", "fogOp", 0.92f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Fog Luminance", "fogLum", 1.4f, 0.0f, 8.0f);
    rowSlide(*canvas.state, panel, "Sun Shafts", "fogS", 40.0f, 0.0f, 128.0f);
    rowSlide(*canvas.state, panel, "Shaft Start", "fogIn", 0.0f, 0.0f, 80.0f);
    const uint16_t weather = uiPropSection(*canvas.state, details, "Weather", catRender, kSecWeather);
    const uint16_t sunFold = uiFoldBody(*canvas.state, weather, "Sun");
    panel = sunFold;
    tick("Enabled", "sun", true);
    tick("Cast Shadows", "sunCast", true);
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "ReSTIR DI Shadows", sunShRestir, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Cascaded Shadow Maps", sunShCsm, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, sunFold, "Shadows", "ReSTIR DI Shadows", menu), "sunShBtn");
    }
    hideCheck("ReSTIR DI Shadows", "sunSh0", 1, 11, true);
    hideCheck("Cascaded Shadow Maps", "sunSh1", 1, 11, false);
    hideCheck("shadow", "shadow", 0, 0, true);
    {
        const uint16_t swatch = uiPropSwatch(*canvas.state, sunFold, "Color", 1.0f, 0.92f, 0.76f);
        uiName(*canvas.state, swatch, "sunSwatch");
        uiBind(*canvas.state, swatch, onSunSwatch, hud);
    }
    hideSlide("sunR", 1.0f, 0.0f, 1.0f);
    hideSlide("sunG", 0.92f, 0.0f, 1.0f);
    hideSlide("sunB", 0.76f, 0.0f, 1.0f);
    rowSlide(*canvas.state, sunFold, "Intensity", "sunI", 1.0f, 0.0f, 4.0f);
    rowSlide(*canvas.state, sunFold, "Temperature", "sunK", 5600.0f, 1500.0f, 12000.0f);
    rowSlide(*canvas.state, sunFold, "Sun Angle", "sunAngle", 0.53f, 0.05f, 8.0f);
    rowSlide(*canvas.state, sunFold, "Soft Angle", "sunSoft", 0.5f, 0.0f, 8.0f);
    rowSlide(*canvas.state, sunFold, "Indirect Light", "sunIndirect", 1.0f, 0.0f, 4.0f);
    {
        const UiPropVec2 row = uiPropVec2(*canvas.state, sunFold, "Rotation", 35.0f, 30.0f, -kOpen, kOpen, 0.5f);
        uiName(*canvas.state, row.x, "zenith");
        uiName(*canvas.state, row.y, "azimuth");
    }
    tick("Sun Follows Clock", "wxClock", false);
    panel = weather;
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "Clear", wxClear, hud);
        uiCommandAddTo(*canvas.state, menu, "Overcast", wxOvercast, hud);
        uiCommandAddTo(*canvas.state, menu, "Rain", wxRain, hud);
        uiCommandAddTo(*canvas.state, menu, "Storm", wxStorm, hud);
        uiName(*canvas.state, uiPropMenu(*canvas.state, weather, "Preset", "Rain", menu), "wxBtn");
    }
    rowSlide(*canvas.state, panel, "Time of day", "wxTime", 11.4f, 0.0f, 24.0f);
    rowSlide(*canvas.state, panel, "Day length", "wxDay", 1200.0f, 60.0f, 7200.0f);
    rowSlide(*canvas.state, panel, "Wind", "wind", 6.0f, 0.0f, 40.0f);
    rowSlide(*canvas.state, panel, "Wind yaw", "windYaw", 23.0f, 0.0f, 360.0f);
    rowSlide(*canvas.state, panel, "Temperature", "wxTemp", 12.0f, -20.0f, 45.0f);
    const uint16_t rainFold = uiFoldBody(*canvas.state, weather, "Rain");
    panel = rainFold;
    rowSlide(*canvas.state, panel, "Amount", "rain", 3.0f, 0.0f, 4.0f);
    rowSlide(*canvas.state, panel, "Wetness", "rainWet", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Ripples", "rainRip", 0.85f, 0.0f, 1.0f);
    const uint16_t cloudFold = uiFoldBody(*canvas.state, weather, "Clouds");
    panel = cloudFold;
    tick("Enabled", "clouden", true);
    tick("Sky", "sky", true);
    rowSlide(*canvas.state, panel, "Coverage", "clouds", 0.42f, 0.0f, 0.95f);
    rowSlide(*canvas.state, panel, "Density", "cdens", 0.08f, 0.0f, 0.3f);
    panel = uiPropSection(*canvas.state, details, "Lights", catRender, kSecPost);
    tick("Cluster", "cluster", true);
    tick("Points", "points", true);
    tick("Spots", "spots", true);
    tick("Area", "area", true);
    rowSlide(*canvas.state, panel, "Candles", "lamp", 1.0f, 0.0f, 1.0f);
    panel = uiPropSection(*canvas.state, details, "LOD", catLod, kSecPost);
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "Freeze Frustum"), "freeze");
    tick("Cone", "cone", true);
    tick("Hi-Z", "hiz", true);
    tick("LOD", "lod", true);
    uiName(*canvas.state, uiPropNumber(*canvas.state, panel, "Force LOD", -1.0f, -1.0f, 3.0f, 1.0f), "lodf");
    tick("Frustum", "frust", true);
    tick("Small", "small", true);
    tick("Backface", "backf", true);
    tick("Screen", "scrn", true);
    tick("Behind", "behind", true);
    panel = uiPropSection(*canvas.state, details, "Streaming", catStream, kSecPost);
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "Virtual textures"), "vt");
    panel = uiPropSection(*canvas.state, details, "Shading", catMisc, kSecPost);
    rowSlide(*canvas.state, panel, "Toksvig", "tok", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Parallax", "pom", 0.35f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Multi-scatter", "ggx", 1.0f, 0.0f, 1.0f);
    panel = modes;
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "Lit", 1, 1, false), "mode0");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "Normals", 1, 1, false), "mode2");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "Primitive ID", 1, 1, false), "mode1");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "GI", 1, 1, false), "mode4");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "SHARC сетка", 1, 1, false), "mode5");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "контакт", 1, 1, false), "mode6");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "луч", 1, 1, false), "mode7");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "прямой", 1, 1, false), "mode8");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "альбедо", 1, 1, false), "mode9");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "солнце", 1, 1, false), "mode10");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "шероховатость", 1, 1, false), "mode11");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "металл", 1, 1, false), "mode12");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "AO", 1, 1, false), "mode13");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "свечение", 1, 1, false), "mode14");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "ReSTIR свет", 1, 1, false), "mode15");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "ReSTIR вес", 1, 1, false), "mode16");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "ReSTIR M", 1, 1, false), "mode17");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "ReSTIR тень", 1, 1, false), "mode18");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "ReSTIR индекс", 1, 1, false), "mode19");
    panel = stats;
    UiFlex statScrollFlex{};
    statScrollFlex.grow = 1.0f;
    statScrollFlex.shrink = 1.0f;
    statScrollFlex.minH = 240.0f;
    statScrollFlex.heightMode = UiSize::Px;
    statScrollFlex.height = 640.0f;
    const uint16_t statView = uiPanel(*canvas.state, panel, statScrollFlex, hiddenPaint());
    UiFlex statLinesFlex{};
    statLinesFlex.gap = 2.0f;
    statLinesFlex.shrink = 0.0f;
    const uint16_t statLines = uiPanel(*canvas.state, statView, statLinesFlex, hiddenPaint());
    if (canvas.state != nullptr) {
        uiBindScroll(*canvas.state, statView, statLines);
    }
    for (int i = 0; i < 40; ++i) {
        char name[8];
        std::snprintf(name, sizeof(name), "g%d", i);
        uiName(*canvas.state, uiLabel(*canvas.state, statLines, ""), name);
    }
    const char* on[] = {"hiz", "cone", "lod", "frust", "small", "backf", "scrn", "behind", "shadow", "cluster", "probes", "gtaoen", "fsr", "bloomen"};
    for (const char* name : on) {
        if (canvas.state != nullptr) {
            uiBindCheck(*canvas.state, uiFindName(*canvas.state, name), 0, 0, true);
        }
    }
    if (canvas.state != nullptr) {
        applyDirect(*canvas.state, 1);
        applyGi(*canvas.state, 3);
        applyReflect(*canvas.state, 2);
        applyAo(*canvas.state, 1);
        applyAa(*canvas.state, 3);
        applyFsrQ(*canvas.state, 0);
        uiBindCheck(*canvas.state, uiFindName(*canvas.state, "mode0"), 1, 1, true);
        uiBindCheck(*canvas.state, uiFindName(*canvas.state, "tone0"), 1, 2, true);
        bindHudIds(*hud, *canvas.state);
        uint8_t mask = kSecTransform;
        if (hud->tool.scene != nullptr) {
            const uint8_t sel = sceneFrameActorSelected(hud->tool.scene);
            if (sel != kSceneActorNone) {
                mask = sceneFrameActorSections(hud->tool.scene, sel);
            }
        }
        uiPropShowMask(*canvas.state, mask);
        uiEnsureColorPopup(*canvas.state);
    }
}

float slideAt(const Canvas& canvas, uint16_t id, float fallback) {
    float value = fallback;
    float minV = 0.0f;
    float maxV = 1.0f;
    if (canvas.state == nullptr || !uiRange(*canvas.state, id, value, minV, maxV)) {
        return fallback;
    }
    return value;
}

bool checkOn(const Canvas& canvas, uint16_t id, bool fallback) {
    const int on = canvas.state != nullptr ? uiCheckOn(*canvas.state, id) : -1;
    return on < 0 ? fallback : on != 0;
}

void pullProp(HudContext& hud, Canvas& canvas, uint8_t index) {
    ActorPose pose{};
    ActorPbr pbr{};
    pbr.base[0] = pbr.base[1] = pbr.base[2] = 1.0f;
    pbr.roughness = 0.5f;
    (void)sceneFrameActorPose(hud.tool.scene, index, pose);
    (void)sceneFrameActorPbr(hud.tool.scene, index, pbr);
    const HudIds& id = hud.ids;
    setSlide(canvas, id.pbrR, pbr.base[0]);
    setSlide(canvas, id.pbrG, pbr.base[1]);
    setSlide(canvas, id.pbrB, pbr.base[2]);
    setSlide(canvas, id.pbrRough, pbr.roughness);
    setSlide(canvas, id.pbrMetal, pbr.metallic);
    setSlide(canvas, id.pbrEm, pbr.emissive);
    setSlide(canvas, id.pbrPx, pose.pos[0]);
    setSlide(canvas, id.pbrPy, pose.pos[1]);
    setSlide(canvas, id.pbrPz, pose.pos[2]);
    setSlide(canvas, id.pbrEx, pose.euler[0]);
    setSlide(canvas, id.pbrEy, pose.euler[1]);
    setSlide(canvas, id.pbrEz, pose.euler[2]);
    setSlide(canvas, id.pbrSx, pose.scale[0]);
    setSlide(canvas, id.pbrSy, pose.scale[1]);
    setSlide(canvas, id.pbrSz, pose.scale[2]);
    if (canvas.state != nullptr) {
        uiBindCheck(*canvas.state, id.xfStatic, 0, 0, pose.isStatic != 0);
    }
    hud.propStatic = pose.isStatic;
    hud.propLast[0] = pbr.base[0];
    hud.propLast[1] = pbr.base[1];
    hud.propLast[2] = pbr.base[2];
    hud.propLast[3] = pbr.roughness;
    hud.propLast[4] = pbr.metallic;
    hud.propLast[5] = pbr.emissive;
    hud.propLast[6] = pose.pos[0];
    hud.propLast[7] = pose.pos[1];
    hud.propLast[8] = pose.pos[2];
    hud.propLast[9] = pose.euler[0];
    hud.propLast[10] = pose.euler[1];
    hud.propLast[11] = pose.euler[2];
    hud.propLast[12] = pose.scale[0];
    hud.propLast[13] = pose.scale[1];
    hud.propLast[14] = pose.scale[2];
    float pivot[3]{};
    (void)sceneFrameActorPivot(hud.tool.scene, index, pivot);
    setSlide(canvas, id.pbrOx, pivot[0]);
    setSlide(canvas, id.pbrOy, pivot[1]);
    setSlide(canvas, id.pbrOz, pivot[2]);
    hud.propPivot[0] = pivot[0];
    hud.propPivot[1] = pivot[1];
    hud.propPivot[2] = pivot[2];
    hud.propSel = index;
}

void syncProp(Engine& e, Canvas& canvas) {
    if (e.scene == nullptr) {
        return;
    }
    const uint8_t selected = sceneFrameActorSelected(e.scene);
    if (selected >= sceneFrameActorCount(e.scene)) {
        e.hud.propSel = 0xff;
        return;
    }
    const bool prop = sceneFrameActorKind(e.scene, selected) == static_cast<uint8_t>(SceneActorKind::Prop);
    if (e.hud.propSel != selected) {
        pullProp(e.hud, canvas, selected);
        return;
    }
    const HudIds& id = e.hud.ids;
    float cur[15] = {
        slideAt(canvas, id.pbrR, e.hud.propLast[0]),
        slideAt(canvas, id.pbrG, e.hud.propLast[1]),
        slideAt(canvas, id.pbrB, e.hud.propLast[2]),
        slideAt(canvas, id.pbrRough, e.hud.propLast[3]),
        slideAt(canvas, id.pbrMetal, e.hud.propLast[4]),
        slideAt(canvas, id.pbrEm, e.hud.propLast[5]),
        slideAt(canvas, id.pbrPx, e.hud.propLast[6]),
        slideAt(canvas, id.pbrPy, e.hud.propLast[7]),
        slideAt(canvas, id.pbrPz, e.hud.propLast[8]),
        slideAt(canvas, id.pbrEx, e.hud.propLast[9]),
        slideAt(canvas, id.pbrEy, e.hud.propLast[10]),
        slideAt(canvas, id.pbrEz, e.hud.propLast[11]),
        slideAt(canvas, id.pbrSx, e.hud.propLast[12]),
        slideAt(canvas, id.pbrSy, e.hud.propLast[13]),
        slideAt(canvas, id.pbrSz, e.hud.propLast[14]),
    };
    if (canvas.state != nullptr) {
        const uint16_t locks[3] = {e.hud.xformLock[0], e.hud.xformLock[1], e.hud.xformLock[2]};
        const uint16_t fields[3][3] = {
            {id.pbrPx, id.pbrPy, id.pbrPz},
            {id.pbrEx, id.pbrEy, id.pbrEz},
            {id.pbrSx, id.pbrSy, id.pbrSz},
        };
        for (int row = 0; row < 3; ++row) {
            if (!uiPropLinked(*canvas.state, locks[row])) {
                continue;
            }
            int changed = -1;
            int count = 0;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::fabs(cur[6 + row * 3 + axis] - e.hud.propLast[6 + row * 3 + axis]) > 0.0005f) {
                    changed = axis;
                    ++count;
                }
            }
            if (count != 1 || changed < 0) {
                continue;
            }
            const float oldV = e.hud.propLast[6 + row * 3 + changed];
            const float newV = cur[6 + row * 3 + changed];
            for (int axis = 0; axis < 3; ++axis) {
                if (axis == changed) {
                    continue;
                }
                float next = e.hud.propLast[6 + row * 3 + axis];
                if (row == 2) {
                    next = std::fabs(oldV) < 1.0e-6f ? newV : next * (newV / oldV);
                } else {
                    next += newV - oldV;
                }
                cur[6 + row * 3 + axis] = next;
                setSlide(canvas, fields[row][axis], next);
            }
        }
    }
    {
        float piv[3] = {
            slideAt(canvas, id.pbrOx, e.hud.propPivot[0]),
            slideAt(canvas, id.pbrOy, e.hud.propPivot[1]),
            slideAt(canvas, id.pbrOz, e.hud.propPivot[2]),
        };
        if (canvas.state != nullptr && uiPropLinked(*canvas.state, e.hud.xformLock[3])) {
            int changed = -1;
            int count = 0;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::fabs(piv[axis] - e.hud.propPivot[axis]) > 0.0005f) {
                    changed = axis;
                    ++count;
                }
            }
            if (count == 1 && changed >= 0) {
                const float delta = piv[changed] - e.hud.propPivot[changed];
                const uint16_t fields[3] = {id.pbrOx, id.pbrOy, id.pbrOz};
                for (int axis = 0; axis < 3; ++axis) {
                    if (axis == changed) {
                        continue;
                    }
                    piv[axis] = e.hud.propPivot[axis] + delta;
                    setSlide(canvas, fields[axis], piv[axis]);
                }
            }
        }
        bool pivotMoved = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::fabs(piv[axis] - e.hud.propPivot[axis]) > 0.0005f) {
                pivotMoved = true;
            }
        }
        if (pivotMoved) {
            sceneFrameActorSetPivot(e.scene, selected, piv);
            for (int axis = 0; axis < 3; ++axis) {
                e.hud.propPivot[axis] = piv[axis];
            }
        } else {
            float got[3]{};
            if (sceneFrameActorPivot(e.scene, selected, got)) {
                bool driftPivot = false;
                for (int axis = 0; axis < 3; ++axis) {
                    if (std::fabs(got[axis] - e.hud.propPivot[axis]) > 0.0005f) {
                        driftPivot = true;
                    }
                }
                if (driftPivot) {
                    setSlide(canvas, id.pbrOx, got[0]);
                    setSlide(canvas, id.pbrOy, got[1]);
                    setSlide(canvas, id.pbrOz, got[2]);
                    for (int axis = 0; axis < 3; ++axis) {
                        e.hud.propPivot[axis] = got[axis];
                    }
                }
            }
        }
    }
    bool moved = checkOn(canvas, id.xfStatic, false) != (e.hud.propStatic != 0);
    for (int i = prop ? 0 : 6; i < 15; ++i) {
        if (std::fabs(cur[i] - e.hud.propLast[i]) > 0.0005f) {
            moved = true;
        }
    }
    if (moved) {
        ActorPbr pbr{};
        pbr.base[0] = cur[0];
        pbr.base[1] = cur[1];
        pbr.base[2] = cur[2];
        pbr.roughness = cur[3];
        pbr.metallic = cur[4];
        pbr.emissive = cur[5];
        ActorPose pose{};
        pose.pos[0] = cur[6];
        pose.pos[1] = cur[7];
        pose.pos[2] = cur[8];
        pose.euler[0] = cur[9];
        pose.euler[1] = cur[10];
        pose.euler[2] = cur[11];
        pose.scale[0] = cur[12];
        pose.scale[1] = cur[13];
        pose.scale[2] = cur[14];
        pose.isStatic = checkOn(canvas, id.xfStatic, false) ? 1 : 0;
        if (prop) {
            sceneFrameActorSetPbr(e.scene, selected, pbr);
        }
        sceneFrameActorSetPose(e.scene, selected, pose);
        e.hud.propStatic = pose.isStatic;
        for (int i = 0; i < 15; ++i) {
            e.hud.propLast[i] = cur[i];
        }
        return;
    }
    ActorPose pose{};
    if (!sceneFrameActorPose(e.scene, selected, pose)) {
        return;
    }
    const float got[9] = {
        pose.pos[0], pose.pos[1], pose.pos[2], pose.euler[0], pose.euler[1], pose.euler[2],
        pose.scale[0], pose.scale[1], pose.scale[2],
    };
    bool drift = false;
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(got[i] - e.hud.propLast[6 + i]) > 0.0005f) {
            drift = true;
        }
    }
    if (!drift) {
        return;
    }
    setSlide(canvas, id.pbrPx, pose.pos[0]);
    setSlide(canvas, id.pbrPy, pose.pos[1]);
    setSlide(canvas, id.pbrPz, pose.pos[2]);
    setSlide(canvas, id.pbrEx, pose.euler[0]);
    setSlide(canvas, id.pbrEy, pose.euler[1]);
    setSlide(canvas, id.pbrEz, pose.euler[2]);
    setSlide(canvas, id.pbrSx, pose.scale[0]);
    setSlide(canvas, id.pbrSy, pose.scale[1]);
    setSlide(canvas, id.pbrSz, pose.scale[2]);
    for (int i = 0; i < 9; ++i) {
        e.hud.propLast[6 + i] = got[i];
    }
}

void syncViewport(Engine& e, Canvas& canvas) {
    SceneFrame* scene = e.scene;
    UiState* state = canvas.state.get();
    if (scene == nullptr || state == nullptr || e.hud.camFov == kUiNone) {
        return;
    }
    HudContext& hud = e.hud;
    if (hud.camInit == 0) {
        const float fov = sceneFrameCameraFov(scene);
        const float nearZ = sceneFrameCameraNear(scene);
        const float farZ = sceneFrameCameraFar(scene);
        const float speed = sceneFrameCameraSpeed(scene);
        uiSliderSet(*state, hud.camFov, fov);
        setSlide(canvas, hud.camFovNum, fov);
        setSlide(canvas, hud.camNear, nearZ);
        setSlide(canvas, hud.camFar, farZ);
        setSlide(canvas, hud.camSpeed, speed);
        hud.fovSent = fov;
        hud.nearSent = nearZ;
        hud.farSent = farZ;
        hud.speedSent = speed;
        hud.camInit = 1;
    }
    const float fovSlide = slideAt(canvas, hud.camFov, hud.fovSent);
    const float fovNum = slideAt(canvas, hud.camFovNum, hud.fovSent);
    float fov = hud.fovSent;
    if (std::fabs(fovSlide - hud.fovSent) > 0.05f) {
        fov = fovSlide;
    } else if (std::fabs(fovNum - hud.fovSent) > 0.05f) {
        fov = fovNum;
    }
    const float nearZ = slideAt(canvas, hud.camNear, hud.nearSent);
    const float farZ = slideAt(canvas, hud.camFar, hud.farSent);
    if (std::fabs(fov - hud.fovSent) > 0.05f || std::fabs(nearZ - hud.nearSent) > 0.0005f || std::fabs(farZ - hud.farSent) > 0.5f) {
        sceneFrameCamera(scene, fov, nearZ, farZ);
        hud.fovSent = sceneFrameCameraFov(scene);
        hud.nearSent = sceneFrameCameraNear(scene);
        hud.farSent = sceneFrameCameraFar(scene);
        uiSliderSet(*state, hud.camFov, hud.fovSent);
        setSlide(canvas, hud.camFovNum, hud.fovSent);
        setSlide(canvas, hud.camNear, hud.nearSent);
        setSlide(canvas, hud.camFar, hud.farSent);
    }
    const float liveSpeed = sceneFrameCameraSpeed(scene);
    const float shownSpeed = slideAt(canvas, hud.camSpeed, liveSpeed);
    if (std::fabs(liveSpeed - hud.speedSent) > 0.02f && std::fabs(shownSpeed - hud.speedSent) < 0.02f) {
        setSlide(canvas, hud.camSpeed, liveSpeed);
        hud.speedSent = liveSpeed;
    } else if (std::fabs(shownSpeed - hud.speedSent) > 0.001f) {
        sceneFrameCameraSpeed(scene, shownSpeed);
        hud.speedSent = sceneFrameCameraSpeed(scene);
        setSlide(canvas, hud.camSpeed, hud.speedSent);
    }
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const float shown = slideAt(canvas, hud.snapNum[channel], hud.snapSent[channel]);
        if (std::fabs(shown - hud.snapSent[channel]) > 0.0001f) {
            hud.snapSent[channel] = shown;
            sceneFrameSnap(scene, channel, hud.snapOn[channel], shown);
        }
    }
    const uint8_t proj = checkOn(canvas, hud.camProj[1], false) ? 1 : 0;
    if (proj != sceneFrameCameraProject(scene)) {
        sceneFrameCameraProject(scene, proj);
    }
    if (hud.camBreak != 0) {
        hud.camBreak = 0;
        if (sceneFrameCameraMode(scene) != 0 && state != nullptr) {
            sceneFrameCameraRelease(scene);
            for (uint8_t i = 0; i < 6; ++i) {
                if (hud.camAxis[i] < state->count) {
                    if (auto* check = state->ent[hud.camAxis[i]].try_get_mut<UiCheck>()) {
                        check->on = 0;
                    }
                }
            }
            hud.viewSent = 0;
            state->visualDirty = true;
        }
    }
    uint8_t view = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        if (checkOn(canvas, hud.camAxis[i], false)) {
            view = static_cast<uint8_t>(i + 1);
        }
    }
    if (view != hud.viewSent) {
        sceneFrameCameraView(scene, view);
        hud.viewSent = view;
    }
    if (hud.camPanel == kUiNone) {
        return;
    }
    auto placeUnder = [&](uint16_t panel, uint16_t button, float width, bool right) {
        if (panel >= state->count || button >= state->count) {
            return;
        }
        if (auto* flex = state->ent[panel].try_get_mut<UiFlex>()) {
            const UiBox& box = state->box[button];
            flex->position = UiPosition::Absolute;
            float x = right ? box.x + box.w - width : box.x;
            if (x + width > state->laidW - 4.0f) {
                x = state->laidW - width - 4.0f;
            }
            if (x < 4.0f) {
                x = 4.0f;
            }
            flex->posX = x;
            float y = box.y + box.h + 4.0f;
            uint16_t bar = state->parentOf[button];
            if (bar < state->count) {
                bar = state->parentOf[bar];
            }
            if (bar < state->count && state->box[bar].h > 1.0f) {
                const float under = state->box[bar].y + state->box[bar].h + 4.0f;
                if (under > y) {
                    y = under;
                }
            }
            flex->posY = y;
        }
    };
    if (hud.camWant == 2 && (!state->menuOpen || uiMenuMetrics(*state).opened != hud.camPanel)) {
        hud.camWant = 0;
    }
    if ((hud.camWant == 1 || hud.camWant == 2) && hud.camBtn != kUiNone) {
        placeUnder(hud.camPanel, hud.camBtn, 400.0f, true);
        if (hud.camWant == 1) {
            uiReveal(*state, hud.camPanel, 500.0f, true);
            uiMenuMetrics(*state).opened = hud.camPanel;
            state->menuOpen = true;
            state->layoutDirty = true;
            hud.camWant = 2;
        }
    } else if (hud.camWant == 0) {
        uiReveal(*state, hud.camPanel, 500.0f, false);
        if (uiMenuMetrics(*state).opened == hud.camPanel) {
            uiMenuMetrics(*state).opened = kUiNone;
            state->menuOpen = false;
        }
    }
    if (hud.viewWant == 2 && (!state->menuOpen || uiMenuMetrics(*state).opened != hud.viewPanel)) {
        hud.viewWant = 0;
    }
    if ((hud.viewWant == 1 || hud.viewWant == 2) && hud.viewBtn != kUiNone && hud.viewPanel < state->count) {
        float viewW = 160.0f;
        if (const UiFlex* flex = state->ent[hud.viewPanel].try_get<UiFlex>()) {
            viewW = flex->width;
        }
        placeUnder(hud.viewPanel, hud.viewBtn, viewW, true);
        if (hud.viewWant == 1) {
            uiReveal(*state, hud.viewPanel, hud.viewPopH, true);
            uiMenuMetrics(*state).opened = hud.viewPanel;
            state->menuOpen = true;
            state->layoutDirty = true;
            hud.viewWant = 2;
        }
    } else if (hud.viewWant == 0 && hud.viewPanel < state->count) {
        uiReveal(*state, hud.viewPanel, hud.viewPopH, false);
        if (uiMenuMetrics(*state).opened == hud.viewPanel) {
            uiMenuMetrics(*state).opened = kUiNone;
            state->menuOpen = false;
        }
    }
    bool anyStat = false;
    for (uint8_t i = 0; i < 6; ++i) {
        hud.statOn[i] = checkOn(canvas, hud.statCheck[i], false) ? 1 : 0;
        uiShow(*state, hud.statLine[i], hud.statOn[i] != 0);
        anyStat = anyStat || hud.statOn[i] != 0;
    }
    uiShow(*state, hud.overlay, anyStat);
    if (anyStat && hud.overlay < state->count) {
        HostMeters meters{};
        platformHostMeters(meters);
        const double ram = static_cast<double>(meters.rss) / (1024.0 * 1024.0);
        const double vram = static_cast<double>(deviceMemoryUsed(e.host.device)) / (1024.0 * 1024.0);
        const float fps = sceneFrameFps(scene);
        const float frame = fps > 1.0f ? 1000.0f / fps : 0.0f;
        char line[6][64]{};
        std::snprintf(line[0], sizeof(line[0]), "FPS %.0f   %.2f ms", static_cast<double>(fps), static_cast<double>(frame));
        std::snprintf(line[1], sizeof(line[1]), "GPU %.2f ms", static_cast<double>(sceneFrameGpuMs(scene)));
        std::snprintf(line[2], sizeof(line[2]), "Draws %u   Tris %u", sceneFrameDraws(scene), sceneFrameTriangles(scene));
        std::snprintf(line[3], sizeof(line[3]), "Physics %.2f ms", static_cast<double>(sceneFramePhysicsMs(scene)));
        std::snprintf(line[4], sizeof(line[4]), "RAM %.0f MB", ram);
        std::snprintf(line[5], sizeof(line[5]), "VRAM %.0f MB", vram);
        for (uint8_t i = 0; i < 6; ++i) {
            if (hud.statOn[i] != 0) {
                uiText(*state, hud.statLine[i], line[i]);
            }
        }
        if (auto* flex = state->ent[hud.overlay].try_get_mut<UiFlex>()) {
            flex->position = UiPosition::Absolute;
            flex->posX = 8.0f;
            flex->posY = 8.0f;
        }
    }
    if (hud.statsWant == 2 && hud.statsPanel < state->count && (!state->menuOpen || uiMenuMetrics(*state).opened != hud.statsPanel)) {
        hud.statsWant = 0;
    }
    if ((hud.statsWant == 1 || hud.statsWant == 2) && hud.gearBtn != kUiNone && hud.statsPanel < state->count) {
        placeUnder(hud.statsPanel, hud.gearBtn, 280.0f, true);
        if (hud.statsWant == 1) {
            uiReveal(*state, hud.statsPanel, 252.0f, true);
            uiMenuMetrics(*state).opened = hud.statsPanel;
            state->menuOpen = true;
            state->layoutDirty = true;
            hud.statsWant = 2;
        }
    } else if (hud.statsWant == 0 && hud.statsPanel < state->count) {
        uiReveal(*state, hud.statsPanel, 252.0f, false);
        if (uiMenuMetrics(*state).opened == hud.statsPanel) {
            uiMenuMetrics(*state).opened = kUiNone;
            state->menuOpen = false;
        }
    }
}

bool engineTick(Engine& e) {
    char picked[512]{};
    if (windowTakeFile(picked, static_cast<int>(sizeof(picked))) && e.demo.fileDialogSlot >= 0) {
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
            uiText(hudState, uiFindName(hudState, "stats"), fps);
            uiText(hudState, uiFindName(hudState, "mem"), memLine);
            uiText(hudState, uiFindName(hudState, "load"), loadLine);
        }
        Canvas& hud = e.host.view[e.host.primary].canvas;
        const uint8_t selected = sceneFrameActorSelected(e.scene);
        if (selected != e.hud.shown && selected != kSceneActorNone) {
            e.hud.shown = selected;
            if (hud.state != nullptr) {
                uiPropShowMask(*hud.state, sceneFrameActorSections(e.scene, selected));
            }
        }
        if (hud.state != nullptr) {
            showNamed(*hud.state, "bloomBody", checkOn(hud, e.hud.ids.bloomen, true));
            showNamed(*hud.state, "sscsBody", checkOn(hud, e.hud.ids.contacten, true));
            showNamed(*hud.state, "fogBody", checkOn(hud, e.hud.ids.fogen, true));
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
        const HudIds& id = e.hud.ids;
        uint32_t mode = 0;
        for (uint32_t i = 0; i < 20; ++i) {
            if (checkOn(hud, id.mode[i], false)) {
                mode = i;
            }
        }
        sceneFrameDebug(e.scene,
            checkOn(hud, id.hiz, true), checkOn(hud, id.cone, true),
            checkOn(hud, id.lod, true), checkOn(hud, id.frust, true), checkOn(hud, id.small, true), checkOn(hud, id.shadow, true),
            checkOn(hud, id.backface, true), checkOn(hud, id.screen, true), checkOn(hud, id.behind, true),
            mode);
        HudPost hudPost{};
        hudPost.brightness = slideAt(hud, id.bright, 1.0f);
        hudPost.grain = slideAt(hud, id.grain, hudPost.grain);
        hudPost.bloom = slideAt(hud, id.bloom, hudPost.bloom);
        hudPost.vignette = slideAt(hud, id.vig, hudPost.vignette);
        hudPost.ca = slideAt(hud, id.ca, hudPost.ca);
        hudPost.contact = slideAt(hud, id.contact, hudPost.contact);
        hudPost.specOcc = slideAt(hud, id.spec, hudPost.specOcc);
        hudPost.toksvig = slideAt(hud, id.tok, hudPost.toksvig);
        hudPost.pom = slideAt(hud, id.pom, hudPost.pom);
        hudPost.ggx = slideAt(hud, id.ggx, hudPost.ggx);
        hudPost.gtao = slideAt(hud, id.gtao, hudPost.gtao);
        hudPost.lamp = slideAt(hud, id.lamp, hudPost.lamp);
        hudPost.sunGain = slideAt(hud, id.sunI, 1.0f);
        hudPost.sunR = slideAt(hud, id.sunR, 1.0f);
        hudPost.sunG = slideAt(hud, id.sunG, 0.92f);
        hudPost.sunB = slideAt(hud, id.sunB, 0.76f);
        hudPost.exposure = slideAt(hud, id.exposure, -1.0f);
        hudPost.temperature = slideAt(hud, id.temperature, 0.0f);
        hudPost.tint = slideAt(hud, id.tint, 0.0f);
        hudPost.contrast = slideAt(hud, id.contrast, 1.0f);
        hudPost.saturation = slideAt(hud, id.saturation, 1.0f);
        hudPost.night = slideAt(hud, id.night, 0.0f);
        hudPost.shadows = slideAt(hud, id.shadows, 1.0f);
        hudPost.midtones = slideAt(hud, id.midtones, 1.0f);
        hudPost.highlights = slideAt(hud, id.highlights, 1.0f);
        hudPost.slope = slideAt(hud, id.slope, 1.0f);
        hudPost.cdlOffset = slideAt(hud, id.cdlOffset, 0.0f);
        hudPost.power = slideAt(hud, id.power, 1.0f);
        hudPost.dofFocus = slideAt(hud, id.dofFocus, 8.0f);
        hudPost.dofAmount = slideAt(hud, id.dofAmount, 0.0f);
        hudPost.flare = slideAt(hud, id.flare, 0.0f);
        hudPost.mix = slideAt(hud, id.mix, 0.0f);
        hudPost.genContrast = slideAt(hud, id.genContrast, 1.0f);
        hudPost.midIn = slideAt(hud, id.midIn, 0.18f);
        hudPost.midOut = slideAt(hud, id.midOut, 0.18f);
        hudPost.hdrMax = slideAt(hud, id.hdrMax, 10.0f);
        hudPost.vibrance = slideAt(hud, id.vibrance, 1.0f);
        float gradeFlags = 0.0f;
        if (checkOn(hud, id.fxaa, false)) {
            gradeFlags += 1.0f;
        }
        if (checkOn(hud, id.lumScale, false)) {
            gradeFlags += 2.0f;
        }
        if (checkOn(hud, id.gamut, false)) {
            gradeFlags += 4.0f;
        }
        if (checkOn(hud, id.taa, false)) {
            gradeFlags += 8.0f;
        }
        hudPost.gradeFlags = gradeFlags;
        sceneFrameTune(e.scene, hudPost);
        SceneTweaks tweaks{};
        tweaks.forceLod = static_cast<int32_t>(slideAt(hud, id.lodf, -1.0f));
        tweaks.gtaoRadius = slideAt(hud, id.gtaor, 0.5f);
        tweaks.gtaoSteps = slideAt(hud, id.gtaoSt, 3.0f);
        tweaks.gtaoSlices = slideAt(hud, id.gtaoSl, 3.0f);
        tweaks.gtaoPower = slideAt(hud, id.gtaoPw, 2.2f);
        tweaks.gtaoFalloff = slideAt(hud, id.gtaoFa, 0.615f);
        tweaks.gtaoDenoise = slideAt(hud, id.gtaoDn, 1.0f);
        tweaks.ssrCutoff = slideAt(hud, id.ssrCut, 0.82f);
        tweaks.ssrSteps = slideAt(hud, id.ssrSteps, 64.0f);
        tweaks.ssrThick = slideAt(hud, id.ssrThick, 0.02f);
        tweaks.ssrMetal = slideAt(hud, id.ssrMetal, 0.35f);
        tweaks.ssrRt = checkOn(hud, id.reflRt, true) ? slideAt(hud, id.ssrRt, 0.45f) : 0.0f;
        tweaks.ssrEdge = slideAt(hud, id.ssrEdge, 24.0f);
        tweaks.ssrMax = slideAt(hud, id.ssrMax, 80.0f);
        tweaks.ssrStride = slideAt(hud, id.ssrBlur, 1.0f);
        tweaks.sscsReach = slideAt(hud, id.sscsR, 1.0f);
        tweaks.sscsThick = slideAt(hud, id.sscsT, 0.02f);
        tweaks.sscsSteps = slideAt(hud, id.sscsS, 8.0f);
        tweaks.sscsPx = slideAt(hud, id.sscsP, 128.0f);
        tweaks.rcRays = slideAt(hud, id.rcRays, 1.0f);
        tweaks.rcSpacing = slideAt(hud, id.rcCell, 0.8f);
        tweaks.rcIntensity = slideAt(hud, id.rcGain, 1.0f);
        tweaks.giGain = slideAt(hud, id.giGain, 2.0f);
        tweaks.sharcDenoise = checkOn(hud, id.denoise, true);
        tweaks.rcMax = slideAt(hud, id.rcMax, 48.0f);
        tweaks.rcSun = slideAt(hud, id.rcSun, 1.0f);
        tweaks.rcBounce = slideAt(hud, id.rcBounce, 1.0f);
        tweaks.restirLocal = slideAt(hud, id.rsLocal, 8.0f);
        tweaks.restirSpatial = slideAt(hud, id.rsSpatial, 3.0f);
        tweaks.restirRadius = slideAt(hud, id.rsRadius, 4.0f);
        tweaks.restirHistory = slideAt(hud, id.rsHistory, 20.0f);
        tweaks.restirBoil = slideAt(hud, id.rsBoil, 0.2f);
        tweaks.restirTemporalDepth = slideAt(hud, id.rsTDepth, 0.1f);
        tweaks.restirTemporalNormal = slideAt(hud, id.rsTNorm, 0.9f);
        tweaks.restirTemporalBias = slideAt(hud, id.rsTBias, 1.0f);
        tweaks.restirSpatialDepth = slideAt(hud, id.rsSDepth, 0.1f);
        tweaks.restirSpatialNormal = slideAt(hud, id.rsSNorm, 0.9f);
        tweaks.restirSpatialBias = slideAt(hud, id.rsSBias, 3.0f);
        tweaks.restirSpatialBoost = slideAt(hud, id.rsSBoost, 3.0f);
        tweaks.restirSunDisk = slideAt(hud, id.rsSun, 0.5f);
        tweaks.restirSunRays = slideAt(hud, id.rsSunRays, 1.0f);
        tweaks.restirShadowRays = slideAt(hud, id.rsShadowRays, 1.0f);
        tweaks.restirShadowRange = slideAt(hud, id.rsShadowRange, 0.0f);
        tweaks.restirShadowBias = slideAt(hud, id.rsShadowBias, 0.05f);
        tweaks.restirPermTh = slideAt(hud, id.rsPermTh, 0.9f);
        tweaks.restirTarget = slideAt(hud, id.rsTarget, 20.0f);
        tweaks.restirTemporal = checkOn(hud, id.rsTOn, true);
        tweaks.restirSpatialOn = checkOn(hud, id.rsSOn, true);
        tweaks.restirBoilOn = checkOn(hud, id.rsBoilOn, true);
        tweaks.restirInitVis = checkOn(hud, id.rsVis, false);
        tweaks.restirPerm = checkOn(hud, id.rsPerm, true);
        tweaks.restirShortcut = checkOn(hud, id.rsShort, false);
        tweaks.restirMaterial = checkOn(hud, id.rsMat, true);
        tweaks.restirNaive = checkOn(hud, id.rsNaive, true);
        tweaks.restirSunOn = checkOn(hud, id.rsSunOn, true);
        tweaks.bloomThreshold = slideAt(hud, id.bloomth, 0.35f);
        tweaks.fogDensity = slideAt(hud, id.fog, 0.06f);
        tweaks.fogHeight = slideAt(hud, id.fogH, 0.0f);
        tweaks.fogScatter = slideAt(hud, id.fogS, 40.0f);
        tweaks.fogFalloff = slideAt(hud, id.fogFall, 0.08f);
        if (hud.state != nullptr) {
            UiState& state = *hud.state;
            const uint16_t swatch = id.fogSwatch;
            UiColorPickerComponent& picker = uiColor(state);
            if (swatch != kUiNone && picker.colorAnchor == swatch && picker.book != nullptr && picker.colorPresent != 0) {
                setSlide(hud, id.fogR, picker.book->r);
                setSlide(hud, id.fogG, picker.book->g);
                setSlide(hud, id.fogB, picker.book->b);
                uiPaintRgb(state, swatch, picker.book->r, picker.book->g, picker.book->b);
            }
            const uint16_t sunSwatch = id.sunSwatch;
            if (sunSwatch != kUiNone && picker.colorAnchor == sunSwatch && picker.book != nullptr && picker.colorPresent != 0) {
                setSlide(hud, id.sunR, picker.book->r);
                setSlide(hud, id.sunG, picker.book->g);
                setSlide(hud, id.sunB, picker.book->b);
                uiPaintRgb(state, sunSwatch, picker.book->r, picker.book->g, picker.book->b);
            }
        }
        tweaks.fogR = slideAt(hud, id.fogR, 0.62f);
        tweaks.fogG = slideAt(hud, id.fogG, 0.72f);
        tweaks.fogB = slideAt(hud, id.fogB, 0.86f);
        tweaks.fogLum = slideAt(hud, id.fogLum, 1.4f);
        tweaks.fogStart = slideAt(hud, id.fogStart, 0.0f);
        tweaks.fogOpacity = slideAt(hud, id.fogOp, 0.92f);
        tweaks.fogInStart = slideAt(hud, id.fogIn, 0.0f);
        tweaks.rainAmount = slideAt(hud, id.rain, 3.0f);
        tweaks.rainWet = slideAt(hud, id.rainWet, 1.0f);
        tweaks.rainRipple = slideAt(hud, id.rainRip, 0.85f);
        const bool clockSun = checkOn(hud, id.wxClock, false);
        float zenith = slideAt(hud, id.zenith, 35.0f);
        float azimuth = slideAt(hud, id.azimuth, 30.0f);
        if (clockSun) {
            zenith = sceneFrameWeatherZenith(e.scene);
            azimuth = sceneFrameWeatherAzimuth(e.scene);
            setSlide(hud, id.zenith, zenith);
            setSlide(hud, id.azimuth, azimuth);
            if (weatherIndex != kSceneActorNone) {
                sceneFrameActorSetEuler(e.scene, weatherIndex, zenith, azimuth);
            }
        } else if (weatherIndex != kSceneActorNone) {
            float pitch = zenith;
            float yaw = azimuth;
            sceneFrameActorEuler(e.scene, weatherIndex, pitch, yaw);
            const bool dragging = sceneFrameActorDragging(e.scene) && selected == weatherIndex;
            if (dragging || (std::fabs(zenith - pitch) <= 0.25f && std::fabs(azimuth - yaw) <= 0.25f)) {
                setSlide(hud, id.zenith, pitch);
                setSlide(hud, id.azimuth, yaw);
                zenith = pitch;
                azimuth = yaw;
            } else {
                sceneFrameActorSetEuler(e.scene, weatherIndex, zenith, azimuth);
            }
        }
        tweaks.sunZenith = zenith;
        tweaks.sunAzimuth = azimuth;
        tweaks.cloudCoverage = slideAt(hud, id.clouds, 0.42f);
        tweaks.cloudDensity = slideAt(hud, id.cdens, 0.08f);
        tweaks.windSpeed = slideAt(hud, id.wind, 6.0f);
        tweaks.windYaw = slideAt(hud, id.windYaw, 23.0f);
        tweaks.temperatureC = slideAt(hud, id.wxTemp, 12.0f);
        tweaks.dayLength = slideAt(hud, id.wxDay, 1200.0f);
        tweaks.timeOfDay = slideAt(hud, id.wxTime, 11.4f);
        tweaks.clockSun = checkOn(hud, id.wxClock, false) ? 1 : 0;
        tweaks.sunEnabled = checkOn(hud, id.sun, true) ? 1 : 0;
        tweaks.sunCast = checkOn(hud, id.sunCast, true) ? 1 : 0;
        tweaks.sunCsm = checkOn(hud, id.sunSh1, false) ? 1 : 0;
        tweaks.sunKelvin = slideAt(hud, id.sunKelvin, 5600.0f);
        tweaks.sunAngle = slideAt(hud, id.sunAngle, 0.53f);
        tweaks.sunSoft = slideAt(hud, id.sunSoft, 0.5f);
        tweaks.sunIndirect = slideAt(hud, id.sunIndirect, 1.0f);
        tweaks.freeze = checkOn(hud, id.freeze, false);
        tweaks.passMask = 0;
        if (checkOn(hud, id.cluster, true)) {
            tweaks.passMask |= 1u;
        }
        if (checkOn(hud, id.probes, true)) {
            tweaks.passMask |= 2u;
        }
        if (checkOn(hud, id.gien, true)) {
            tweaks.passMask |= 262144u;
        }
        if (checkOn(hud, id.fsr, true)) {
            tweaks.passMask |= 4194304u;
        } else if (checkOn(hud, id.cmaa, false)) {
            tweaks.passMask |= 8u;
        }
        if (checkOn(hud, id.gtaoen, true)) {
            tweaks.passMask |= 16u;
        }
        if (checkOn(hud, id.bloomen, true)) {
            tweaks.passMask |= 32u;
        }
        if (checkOn(hud, id.fsr, true) == false && checkOn(hud, id.smaa, false)) {
            tweaks.passMask |= 4u;
        }
        tweaks.fsrQuality = 0;
        for (uint32_t q = 0; q < 5u; ++q) {
            if (checkOn(hud, id.fsrq[q], q == 0u)) {
                tweaks.fsrQuality = q;
            }
        }
        tweaks.fsrSharpness = slideAt(hud, id.fsrSharp, 0.2f);
        if (checkOn(hud, id.sun, true)) {
            tweaks.passMask |= 256u;
        }
        if (checkOn(hud, id.points, true)) {
            tweaks.passMask |= 512u;
        }
        if (checkOn(hud, id.spots, true)) {
            tweaks.passMask |= 65536u;
        }
        if (checkOn(hud, id.area, true)) {
            tweaks.passMask |= 131072u;
        }
        if (checkOn(hud, id.sky, true)) {
            tweaks.passMask |= 1024u;
        }
        if (checkOn(hud, id.clouden, true)) {
            tweaks.passMask |= 2048u;
        }
        if (checkOn(hud, id.fogen, true)) {
            tweaks.passMask |= 4096u;
        }
        if (checkOn(hud, id.reflecten, true)) {
            tweaks.passMask |= 8192u;
        }
        if (checkOn(hud, id.contacten, true)) {
            tweaks.passMask |= 16384u;
        }
        if (checkOn(hud, id.vt, false)) {
            tweaks.passMask |= 32768u;
        }
        tweaks.lightProfile = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            if (checkOn(hud, id.prof[i], i == 3u)) {
                tweaks.lightProfile = i;
            }
        }
        if (checkOn(hud, id.rcen, false)) {
            tweaks.passMask |= 128u;
        }
        if ((tweaks.passMask & 262144u) != 0u && tweaks.lightProfile == 1u && (tweaks.passMask & 128u) == 0u) {
            tweaks.passMask |= 524288u;
        }
        if ((tweaks.passMask & 262144u) != 0u && tweaks.lightProfile == 2u) {
            tweaks.passMask |= 128u;
        }
        if ((tweaks.passMask & 262144u) != 0u && tweaks.lightProfile == 3u) {
            tweaks.passMask |= kPassSharc;
        }
        if (checkOn(hud, id.restiren, true)) {
            tweaks.passMask |= 1048576u;
            if (tweaks.restirSunOn && tweaks.sunCsm == 0 && (tweaks.passMask & 256u) != 0u) {
                tweaks.passMask |= 2097152u;
            }
        }
        tweaks.tonemapper = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            if (checkOn(hud, id.tone[i], false)) {
                tweaks.tonemapper = i;
            }
        }
        sceneFrameTweaks(e.scene, tweaks);
        syncViewport(e, hud);
        const float shownTime = sceneFrameWeatherTime(e.scene);
        if (shownTime >= 0.0f) {
            setSlide(hud, id.wxTime, shownTime);
            if (tweaks.clockSun != 0 && weatherIndex != kSceneActorNone) {
                const float zen = sceneFrameWeatherZenith(e.scene);
                const float az = sceneFrameWeatherAzimuth(e.scene);
                setSlide(hud, id.zenith, zen);
                setSlide(hud, id.azimuth, az);
                sceneFrameActorSetEuler(e.scene, weatherIndex, zen, az);
            }
        }
        sceneFrameKick(e.scene, e.host.device);
        const char* gpuText = sceneFrameGpuText(e.scene);
        char lines[40][96]{};
        uint32_t line = 0;
        const char* cursor = gpuText != nullptr ? gpuText : "";
        while (line < 40 && cursor[0] != '\0') {
            const char* nl = std::strchr(cursor, '\n');
            const size_t n = nl != nullptr ? static_cast<size_t>(nl - cursor) : std::strlen(cursor);
            const size_t copy = n < 95 ? n : 95;
            std::memcpy(lines[line], cursor, copy);
            lines[line][copy] = '\0';
            line += 1;
            if (nl == nullptr) {
                break;
            }
            cursor = nl + 1;
        }
        if (hud.state != nullptr) {
            for (uint32_t i = 0; i < 40; ++i) {
                uiText(*hud.state, id.stat[i], lines[i]);
            }
        }
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok;
}

} // namespace burnhope
