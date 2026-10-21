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
#include "ui/State.hpp"
#include "ui/Canvas.hpp"

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
        "scene/scene.gltf",
        "scene/lighter/vintage_lighter_1k.gltf",
    };
    std::error_code ec;
    std::filesystem::create_directories("cache", ec);
    if (!sceneImportFbxList(paths, 2, "cache/scene.bhop.tmp")) {
        return false;
    }
    std::filesystem::rename("cache/scene.bhop.tmp", "cache/scene.bhop", ec);
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
        if (!std::filesystem::exists("cache/scene.bhop")) {
            spdlog::info("import scene/scene.gltf");
            if (!engineImportScene()) {
                spdlog::error("scene import failed");
            }
        }
        if (!sceneFrameLoadBhop(e.scene, "cache/scene.bhop")) {
            spdlog::error("cache/scene.bhop missing");
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
    }
    uiBind(*canvas.state, rowBtn, onActor, &hit);
    return true;
}

void onGizmoMode(void* user, uint16_t) {
    auto* hit = static_cast<GizmoHit*>(user);
    if (hit == nullptr || hit->tool == nullptr || hit->tool->scene == nullptr) {
        return;
    }
    sceneFrameActorGizmo(hit->tool->scene, hit->mode);
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
    hud.ids.xfStatic = id("xfStatic");
    hud.ids.reflRt = id("reflRt");
    hud.ids.fogSwatch = id("fogSwatch");
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
    if (hit->label != kUiNone) {
        if (hit->canvas->state != nullptr) {
            uiText(*hit->canvas->state, hit->label, title);
        }
    }
}

float slideAt(const Canvas& canvas, uint16_t id, float fallback);

void onFogSwatch(void* user, uint16_t id) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud->tool.canvas->state;
    ColorBook* book = uiColor(s).book;
    if (book != nullptr) {
        book->r = slideAt(*hud->tool.canvas, uiFindName(s, "fogR"), 0.62f);
        book->g = slideAt(*hud->tool.canvas, uiFindName(s, "fogG"), 0.72f);
        book->b = slideAt(*hud->tool.canvas, uiFindName(s, "fogB"), 0.86f);
        book->a = 1.0f;
        book->gen += 1;
    }
    colorOnOpen(&s, id);
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
    hud->pages.clear();
    hud->modes.clear();
    hud->gizmos.clear();
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
    UiFlex barFlex{};
    barFlex.direction = UiFlexDir::Row;
    barFlex.heightMode = UiSize::Px;
    barFlex.height = 50.0f;
    barFlex.minH = 50.0f;
    barFlex.shrink = 0.0f;
    barFlex.align = UiAlign::Center;
    barFlex.pad = 12.0f;
    barFlex.gap = 8.0f;
    const uint16_t top = uiPanel(*canvas.state, root, barFlex, dockPaint());
    uint16_t fileBtn = uiButton(*canvas.state, top, { .label = "File" });
    uiSetHeight(*canvas.state, fileBtn, 28.0f);
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
    uint16_t viewBtn = uiButton(*canvas.state, top, { .label = "Lit" });
    uiSetHeight(*canvas.state, viewBtn, 28.0f);
    uint16_t viewMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state) {
        uiSetContextMenu(*canvas.state, viewBtn, viewMenu);
        uiMarkMenuBar(*canvas.state, viewBtn);
    }
    for (const ViewMode& mode : kViews) {
        hud->modes.push_back({});
        ModeHit& hit = hud->modes.back();
        hit.canvas = &canvas;
        hit.index = mode.index;
        hit.label = viewBtn;
        if (canvas.state != nullptr) {
            (void)uiCommandAddTo(*canvas.state, viewMenu, mode.name, onMode, &hit);
        }
    }
    uint16_t statsBtn = uiButton(*canvas.state, top, { .label = "Stats" });
    uiSetHeight(*canvas.state, statsBtn, 28.0f);
    const char* toolName[] = {"Move", "Rotate", "Scale"};
    for (uint8_t i = 0; i < 3; ++i) {
        uint16_t toolBtn = uiButton(*canvas.state, top, { .label = toolName[i] });
        uiSetHeight(*canvas.state, toolBtn, 28.0f);
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.mode = i;
        uiBind(*canvas.state, toolBtn, onGizmoMode, &gizmo);
    }
    uint16_t dupBtn = uiButton(*canvas.state, top, { .label = "Duplicate" });
    uiSetHeight(*canvas.state, dupBtn, 28.0f);
    uiBind(*canvas.state, dupBtn, onDuplicate, hud);
    UiFlex rowFlex{};
    rowFlex.direction = UiFlexDir::Row;
    rowFlex.grow = 1.0f;
    rowFlex.shrink = 1.0f;
    rowFlex.minH = 72.0f;
    const uint16_t row = uiPanel(*canvas.state, root, rowFlex, hiddenPaint());
    UiFlex viewFlex{};
    viewFlex.grow = 1.0f;
    viewFlex.shrink = 1.0f;
    (void)uiPanel(*canvas.state, row, viewFlex, hiddenPaint());
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
    uiBind(*canvas.state, statsBtn, onStats, &hud->stats);
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
    auto vec = [&](uint16_t host, const char* label, const char* nx, const char* ny, const char* nz, float x, float y, float z, float minV, float maxV, float step) {
        const UiPropVec3 row = uiPropVec3(*canvas.state, host, label, x, y, z, minV, maxV, step);
        uiName(*canvas.state, row.x, nx);
        uiName(*canvas.state, row.y, ny);
        uiName(*canvas.state, row.z, nz);
    };
    const uint8_t catGeneral = static_cast<uint8_t>(UiPropCat::General);
    const uint8_t catActor = static_cast<uint8_t>(UiPropCat::Actor);
    const uint8_t catLod = static_cast<uint8_t>(UiPropCat::Lod);
    const uint8_t catMisc = static_cast<uint8_t>(UiPropCat::Misc);
    const uint8_t catRender = static_cast<uint8_t>(UiPropCat::Rendering);
    const uint8_t catStream = static_cast<uint8_t>(UiPropCat::Streaming);
    panel = uiPropSection(*canvas.state, details, "Transform", catGeneral, kSecTransform);
    vec(panel, "Location", "pbrPx", "pbrPy", "pbrPz", 0.0f, 0.0f, 0.0f, -4000.0f, 4000.0f, 0.05f);
    vec(panel, "Rotation", "pbrEx", "pbrEy", "pbrEz", 0.0f, 0.0f, 0.0f, -180.0f, 180.0f, 0.5f);
    vec(panel, "Scale", "pbrSx", "pbrSy", "pbrSz", 1.0f, 1.0f, 1.0f, 0.05f, 20.0f, 0.05f);
    tick("Static", "xfStatic", false);
    panel = uiPropSection(*canvas.state, details, "Material", catActor, kSecMaterial);
    rowSlide(*canvas.state, panel, "Color R", "pbrR", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Color G", "pbrG", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Color B", "pbrB", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Roughness", "pbrRough", 0.5f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Metallic", "pbrMetal", 0.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Emissive", "pbrEm", 0.0f, 0.0f, 8.0f);
    panel = uiPropSection(*canvas.state, details, "Sun", catRender, kSecSun);
    tick("Enabled", "sun", true);
    tick("Shadows", "shadow", true);
    rowSlide(*canvas.state, panel, "Intensity", "sunI", 1.0f, 0.0f, 4.0f);
    rowSlide(*canvas.state, panel, "Color R", "sunR", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Color G", "sunG", 0.92f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Color B", "sunB", 0.76f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Zenith", "zenith", 35.0f, -10.0f, 80.0f);
    rowSlide(*canvas.state, panel, "Azimuth", "azimuth", 30.0f, 0.0f, 360.0f);
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
    tick("Enabled", "contacten", true);
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
        uiName(*canvas.state, uiPropMenu(*canvas.state, panel, "Anti-aliasing", "FSR", menu), "aaBtn");
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
    const uint16_t cloudFold = uiFoldBody(*canvas.state, atmo, "Clouds");
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
        uint8_t sunIndex = kSceneActorNone;
        if (sceneFrameActorKind(e.scene, selected) == static_cast<uint8_t>(SceneActorKind::Sun)) {
            sunIndex = selected;
        } else {
            const uint8_t count = sceneFrameActorCount(e.scene);
            for (uint8_t i = 0; i < count; ++i) {
                if (sceneFrameActorKind(e.scene, i) == static_cast<uint8_t>(SceneActorKind::Sun)) {
                    sunIndex = i;
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
        }
        tweaks.fogR = slideAt(hud, id.fogR, 0.62f);
        tweaks.fogG = slideAt(hud, id.fogG, 0.72f);
        tweaks.fogB = slideAt(hud, id.fogB, 0.86f);
        tweaks.fogLum = slideAt(hud, id.fogLum, 1.4f);
        tweaks.fogStart = slideAt(hud, id.fogStart, 0.0f);
        tweaks.fogOpacity = slideAt(hud, id.fogOp, 0.92f);
        tweaks.fogInStart = slideAt(hud, id.fogIn, 0.0f);
        float zenith = slideAt(hud, id.zenith, 35.0f);
        float azimuth = slideAt(hud, id.azimuth, 30.0f);
        if (sunIndex != kSceneActorNone) {
            float pitch = zenith;
            float yaw = azimuth;
            sceneFrameActorEuler(e.scene, sunIndex, pitch, yaw);
            const bool dragging = sceneFrameActorDragging(e.scene) && selected == sunIndex;
            if (dragging || (std::fabs(zenith - pitch) <= 0.25f && std::fabs(azimuth - yaw) <= 0.25f)) {
                setSlide(hud, id.zenith, pitch);
                setSlide(hud, id.azimuth, yaw);
                zenith = pitch;
                azimuth = yaw;
            } else {
                sceneFrameActorSetEuler(e.scene, sunIndex, zenith, azimuth);
            }
        }
        tweaks.sunZenith = zenith;
        tweaks.sunAzimuth = azimuth;
        tweaks.cloudCoverage = slideAt(hud, id.clouds, 0.42f);
        tweaks.cloudDensity = slideAt(hud, id.cdens, 0.08f);
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
            if (tweaks.restirSunOn && (tweaks.passMask & 256u) != 0u) {
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
