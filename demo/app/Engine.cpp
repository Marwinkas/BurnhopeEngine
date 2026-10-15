#include "app/Engine.hpp"

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
        (void)sceneFrameLoadBhop(e.scene, "cache/bistro-gltf.bhop");
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
    if (hit->page != kUiNone) {
        uiShowOnly(*hit->canvas->state, hit->page);
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

void applyTone(UiState& s, int which) {
    const char* names[] = {"tone0", "tone1", "tone2", "tone3"};
    const char* caps[] = {"AgX", "ACES", "Reinhard", "Linear"};
    for (int i = 0; i < 4; ++i) {
        uiBindCheck(s, uiFindName(s, names[i]), 1, 2, i == which);
    }
    uiText(s, uiFindName(s, "toneBtn"), caps[which]);
}

void toneAgx(void* user) { applyTone(*static_cast<UiState*>(user), 0); }
void toneAces(void* user) { applyTone(*static_cast<UiState*>(user), 1); }
void toneReinhard(void* user) { applyTone(*static_cast<UiState*>(user), 2); }
void toneLinear(void* user) { applyTone(*static_cast<UiState*>(user), 3); }

void applyAa(UiState& s, int which) {
    uiBindCheck(s, uiFindName(s, "smaa"), 0, 0, which == 1);
    uiBindCheck(s, uiFindName(s, "cmaa"), 0, 0, which == 2);
    const char* caps[] = {"Off", "SMAA", "CMAA"};
    uiText(s, uiFindName(s, "aaBtn"), caps[which]);
}

void aaOff(void* user) { applyAa(*static_cast<UiState*>(user), 0); }
void aaSmaa(void* user) { applyAa(*static_cast<UiState*>(user), 1); }
void aaCmaa(void* user) { applyAa(*static_cast<UiState*>(user), 2); }

void bindHudIds(HudContext& hud, UiState& state) {
    auto id = [&](const char* name) { return uiFindName(state, name); };
    char name[8];
    for (uint32_t i = 0; i < 15; ++i) {
        std::snprintf(name, sizeof(name), "mode%u", i);
        hud.ids.mode[i] = id(name);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        std::snprintf(name, sizeof(name), "tone%u", i);
        hud.ids.tone[i] = id(name);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        std::snprintf(name, sizeof(name), "prof%u", i);
        hud.ids.prof[i] = id(name);
    }
    for (uint32_t i = 0; i < 12; ++i) {
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
    hud.ids.rcMax = id("rcMax");
    hud.ids.bloomth = id("bloomth");
    hud.ids.fog = id("fog");
    hud.ids.fogH = id("fogH");
    hud.ids.fogS = id("fogS");
    hud.ids.zenith = id("zenith");
    hud.ids.azimuth = id("azimuth");
    hud.ids.clouds = id("clouds");
    hud.ids.cdens = id("cdens");
    hud.ids.hiz = id("hiz");
    hud.ids.cone = id("cone");
    hud.ids.lod = id("lod");
    hud.ids.frust = id("frust");
    hud.ids.small = id("small");
    hud.ids.shadow = id("shadow");
    hud.ids.cluster = id("cluster");
    hud.ids.probes = id("probes");
    hud.ids.gien = id("gien");
    hud.ids.cmaa = id("cmaa");
    hud.ids.gtaoen = id("gtaoen");
    hud.ids.bloomen = id("bloomen");
    hud.ids.smaa = id("smaa");
    hud.ids.sun = id("sun");
    hud.ids.points = id("points");
    hud.ids.spots = id("spots");
    hud.ids.area = id("area");
    hud.ids.sky = id("sky");
    hud.ids.clouden = id("clouden");
    hud.ids.fogen = id("fogen");
    hud.ids.reflecten = id("reflecten");
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
};

void onObjectPage(void* user, uint16_t) {
    auto* hit = static_cast<PageHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->page == kUiNone) {
        return;
    }
    uiShowOnly(*hit->canvas->state, hit->page);
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
        flex->width += hit->open != 0 ? 220.0f : -220.0f;
        if (flex->width < 220.0f) {
            flex->width = 220.0f;
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

uint16_t foldOpen(UiState& s, uint16_t parent, const char* title) {
    const uint16_t body = uiTreeRow(s, parent, title);
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* tree = s.ent[id].try_get_mut<UiTree>();
        if (tree != nullptr && tree->body == body) {
            tree->open = 1;
            for (uint16_t child = 0; child < s.count; ++child) {
                if (s.parentOf[child] != id) {
                    continue;
                }
                uiText(s, child, "");
                if (auto* paint = s.ent[child].try_get_mut<UiPaint>()) {
                    paint->a = 0.0f;
                    paint->borderW = 0.0f;
                    paint->shadow = 0.0f;
                    paint->icon = 11;
                    paint->r = s.look.accent[0][0];
                    paint->g = s.look.accent[0][1];
                    paint->b = s.look.accent[0][2];
                }
                break;
            }
            break;
        }
    }
    uiShow(s, body, true);
    return body;
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
        viewFlex.minH = 36.0f;
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
    statsFlex.width = 220.0f;
    statsFlex.shrink = 0.0f;
    statsFlex.minW = 180.0f;
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
    inspFlex.heightMode = UiSize::Px;
    inspFlex.height = 280.0f;
    inspFlex.minH = 40.0f;
    inspFlex.shrink = 1.0f;
    const uint16_t insp = uiPanel(*canvas.state, side, inspFlex, hiddenPaint());
    const uint16_t postInner = scrollPage(insp, true);
    const uint16_t sunInner = scrollPage(insp, false);
    const uint16_t lightInner = scrollPage(insp, false);
    const uint16_t skyInner = scrollPage(insp, false);
    const uint16_t cullInner = scrollPage(insp, false);
    uint16_t post = uiPropHost(*canvas.state, postInner);
    uint16_t sun = uiPropHost(*canvas.state, sunInner);
    uint16_t light = uiPropHost(*canvas.state, lightInner);
    uint16_t sky = uiPropHost(*canvas.state, skyInner);
    uint16_t cull = uiPropHost(*canvas.state, cullInner);
    const uint16_t postView = uiParent(*canvas.state, postInner);
    const uint16_t sunView = uiParent(*canvas.state, sunInner);
    const uint16_t lightView = uiParent(*canvas.state, lightInner);
    const uint16_t skyView = uiParent(*canvas.state, skyInner);
    const uint16_t cullView = uiParent(*canvas.state, cullInner);
    auto pick = [&](const char* title, uint16_t view) {
        uint16_t rowBtn = uiButton(*canvas.state, objects, { .label = title, .fillWidth = true });
        uiSetHeight(*canvas.state, rowBtn, 28.0f);
        if (rowBtn == kUiNone || canvas.state == nullptr) {
            return;
        }
        hud->pages.push_back({});
        PageHit& hit = hud->pages.back();
        hit.canvas = &canvas;
        hit.page = view;
        uiBind(*canvas.state, rowBtn, onObjectPage, &hit);
    };
    hud->tool.postPage = postView;
    hud->tool.sunPage = sunView;
    if (hud->tool.scene != nullptr) {
        const uint8_t count = sceneFrameActorCount(hud->tool.scene);
        for (uint8_t i = 0; i < count; ++i) {
            char name[24];
            uint8_t kind = 0;
            sceneFrameActorName(hud->tool.scene, i, name, sizeof(name), &kind);
            (void)addActorButton(*hud, objects, i, name, kind);
        }
    }
    pick("Light", lightView);
    pick("Sky", skyView);
    pick("Cull", cullView);
    uiShowOnly(*canvas.state, sunView);
    uint16_t fogBody = foldOpen(*canvas.state, post, "Туман");
    uint16_t postBody = foldOpen(*canvas.state, post, "Пост");
    uint16_t giBody = foldOpen(*canvas.state, post, "GI");
    uint16_t sunBody = foldOpen(*canvas.state, sun, "Солнце");
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
    auto head = [&](const char* text, const char* id) {
        uint16_t lab = uiLabel(*canvas.state, panel, text);
        if (id != nullptr) {
            uiName(*canvas.state, lab, id);
        }
        if (auto* paint = canvas.state->ent[lab].try_get_mut<UiPaint>()) {
            paint->token = UiToken::Custom;
            paint->r = look.accent[0][0];
            paint->g = look.accent[0][1];
            paint->b = look.accent[0][2];
            paint->a = 0.0f;
        }
    };
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
    panel = cull;
    head("кулинг", "h1");
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "Freeze Frustum"), "freeze");
    tick("конус", "cone", true);
    tick("Hi-Z", "hiz", true);
    tick("LOD", "lod", true);
    uiName(*canvas.state, uiPropNumber(*canvas.state, panel, "Force LOD", -1.0f, -1.0f, 3.0f, 1.0f), "lodf");
    tick("пирамида", "frust", true);
    tick("мелкие", "small", true);
    panel = light;
    head("свет", "h2");
    tick("кластер ламп", "cluster", true);
    panel = sun;
    tick("тени", "shadow", true);
    panel = giBody;
    tick("зонды SH", "probes", true);
    tick("GI", "gien", true);
    rowSlide(*canvas.state, panel, "GI яркость", "giGain", 2.0f, 0.0f, 6.0f);
    panel = giBody;
    head("профиль света", "h3");
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "Низкий", 1, 3, false), "prof0");
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "Баланс", 1, 3, false), "prof1");
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "Ультра", 1, 3, false), "prof2");
    tick("radiance", "rcen", true);
    rowSlide(*canvas.state, panel, "RC лучи", "rcRays", 1.0f, 1.0f, 4.0f);
    rowSlide(*canvas.state, panel, "RC клетка м", "rcCell", 0.45f, 0.1f, 4.0f);
    rowSlide(*canvas.state, panel, "RC сила", "rcGain", 1.0f, 0.0f, 4.0f);
    rowSlide(*canvas.state, panel, "RC длина м", "rcMax", 48.0f, 2.0f, 200.0f);
    panel = postBody;
    tick("GTAO", "gtaoen", true);
    hideCheck("SMAA", "smaa", 0, 0, false);
    hideCheck("CMAA", "cmaa", 0, 0, true);
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "Off", aaOff, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "SMAA", aaSmaa, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "CMAA", aaCmaa, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, panel, "Сглаживание", "CMAA", menu), "aaBtn");
    }
    tick("Bloom", "bloomen", true);
    panel = sunBody;
    tick("солнце", "sun", true);
    rowSlide(*canvas.state, panel, "солнце", "sunI", 1.0f, 0.0f, 4.0f);
    rowSlide(*canvas.state, panel, "солнце R", "sunR", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "солнце G", "sunG", 0.97f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "солнце B", "sunB", 0.92f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "солнце зенит", "zenith", 35.0f, -10.0f, 80.0f);
    rowSlide(*canvas.state, panel, "солнце азимут", "azimuth", 30.0f, 0.0f, 360.0f);
    panel = light;
    tick("точки", "points", true);
    tick("прожекторы", "spots", true);
    tick("площадь", "area", true);
    panel = sky;
    tick("небо", "sky", true);
    tick("облака", "clouden", true);
    panel = fogBody;
    tick("туман", "fogen", true);
    panel = giBody;
    tick("отражение", "reflecten", true);
    panel = postBody;
    tick("контакт", "contacten", true);
    panel = cull;
    uiName(*canvas.state, uiPropCheck(*canvas.state, panel, "вирт. текстуры"), "vt");
    panel = modes;
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "Lit", 1, 1, false), "mode0");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "Normals", 1, 1, false), "mode2");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "Primitive ID", 1, 1, false), "mode1");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "GI", 1, 1, false), "mode4");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "каскады", 1, 1, false), "mode5");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "контакт", 1, 1, false), "mode6");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "луч", 1, 1, false), "mode7");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "прямой", 1, 1, false), "mode8");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "альбедо", 1, 1, false), "mode9");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "солнце", 1, 1, false), "mode10");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "шероховатость", 1, 1, false), "mode11");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "металл", 1, 1, false), "mode12");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "AO", 1, 1, false), "mode13");
    uiName(*canvas.state, uiCheck(*canvas.state, panel, "свечение", 1, 1, false), "mode14");
    panel = postBody;
    hideCheck("AgX", "tone0", 1, 2, true);
    hideCheck("ACES", "tone1", 1, 2, false);
    hideCheck("Reinhard", "tone2", 1, 2, false); 
    hideCheck("Linear", "tone3", 1, 2, false);
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "AgX", toneAgx, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "ACES", toneAces, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Reinhard", toneReinhard, canvas.state.get());
        uiCommandAddTo(*canvas.state, menu, "Linear", toneLinear, canvas.state.get());
        uiName(*canvas.state, uiPropMenu(*canvas.state, panel, "Цветокор", "AgX", menu), "toneBtn");
    }
    rowSlide(*canvas.state, panel, "яркость", "bright", 1.0f, 0.05f, 4.0f);
    rowSlide(*canvas.state, panel, "зерно", "grain", 0.0f, 0.0f, 0.12f);
    rowSlide(*canvas.state, panel, "блюм", "bloom", 0.15f, 0.0f, 0.80f);
    rowSlide(*canvas.state, panel, "порог блюма", "bloomth", 0.35f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "виньетка", "vig", 0.35f, 0.0f, 2.0f);
    rowSlide(*canvas.state, panel, "хромат. пкс", "ca", 2.0f, 0.0f, 4.0f);
    rowSlide(*canvas.state, panel, "SSCS", "contact", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "SSCS длина м", "sscsR", 0.2f, 0.05f, 2.0f);
    rowSlide(*canvas.state, panel, "SSCS толщина", "sscsT", 0.02f, 0.005f, 0.2f);
    rowSlide(*canvas.state, panel, "SSCS шаги", "sscsS", 12.0f, 4.0f, 16.0f);
    rowSlide(*canvas.state, panel, "SSCS экран px", "sscsP", 128.0f, 4.0f, 256.0f);
    rowSlide(*canvas.state, panel, "окклюзия блика", "spec", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "Toksvig", "tok", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "параллакс", "pom", 0.35f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "multi-scatter", "ggx", 1.0f, 0.0f, 1.0f);
    rowSlide(*canvas.state, panel, "радиус GTAO", "gtaor", 2.5f, 0.2f, 20.0f);
    rowSlide(*canvas.state, panel, "GTAO срезы", "gtaoSl", 6.0f, 1.0f, 8.0f);
    rowSlide(*canvas.state, panel, "GTAO шаги", "gtaoSt", 6.0f, 1.0f, 12.0f);
    rowSlide(*canvas.state, panel, "GTAO сила", "gtaoPw", 1.6f, 0.2f, 4.0f);
    rowSlide(*canvas.state, panel, "GTAO край", "gtaoFa", 1.0f, 0.1f, 1.0f);
    rowSlide(*canvas.state, panel, "GTAO", "gtao", 1.2f, 0.0f, 4.0f);
    panel = light;
    rowSlide(*canvas.state, panel, "свечи", "lamp", 1.0f, 0.0f, 1.0f);
    panel = sky;
    rowSlide(*canvas.state, panel, "облака", "clouds", 0.42f, 0.0f, 0.95f);
    rowSlide(*canvas.state, panel, "плотность", "cdens", 0.08f, 0.0f, 0.3f);
    panel = fogBody;
    rowSlide(*canvas.state, panel, "туман", "fog", 0.002f, 0.0f, 0.08f);
    rowSlide(*canvas.state, panel, "высота тумана", "fogH", 4.0f, -30.0f, 40.0f);
    rowSlide(*canvas.state, panel, "свет в тумане", "fogS", 1.0f, 0.0f, 2.0f);
    panel = stats;
    for (int i = 0; i < 12; ++i) {
        char name[8];
        std::snprintf(name, sizeof(name), "g%d", i);
        uiName(*canvas.state, uiLabel(*canvas.state, panel, ""), name);
    }
    const char* on[] = {"hiz", "cone", "lod", "frust", "small", "shadow", "cluster", "probes", "gtaoen", "cmaa", "bloomen"};
    for (const char* name : on) {
        if (canvas.state != nullptr) {
            uiBindCheck(*canvas.state, uiFindName(*canvas.state, name), 0, 0, true);
        }
    }
    if (canvas.state != nullptr) {
        uiBindCheck(*canvas.state, uiFindName(*canvas.state, "prof0"), 1, 3, true);
        uiBindCheck(*canvas.state, uiFindName(*canvas.state, "mode0"), 1, 1, true);
        uiBindCheck(*canvas.state, uiFindName(*canvas.state, "tone0"), 1, 2, true);
        bindHudIds(*hud, *canvas.state);
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
            const uint8_t kind = sceneFrameActorKind(e.scene, selected);
            uint16_t page = kUiNone;
            if (kind == static_cast<uint8_t>(SceneActorKind::Sun)) {
                page = e.hud.tool.sunPage;
            } else if (kind == static_cast<uint8_t>(SceneActorKind::Post)) {
                page = e.hud.tool.postPage;
            }
            if (page != kUiNone) {
                uiShowOnly(*hud.state, page);
            }
        }
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
        for (uint32_t i = 0; i < 15; ++i) {
            if (checkOn(hud, id.mode[i], false)) {
                mode = i;
            }
        }
        sceneFrameDebug(e.scene,
            checkOn(hud, id.hiz, true), checkOn(hud, id.cone, true),
            checkOn(hud, id.lod, true), checkOn(hud, id.frust, true), checkOn(hud, id.small, true), checkOn(hud, id.shadow, true),
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
        hudPost.sunG = slideAt(hud, id.sunG, 0.97f);
        hudPost.sunB = slideAt(hud, id.sunB, 0.92f);
        sceneFrameTune(e.scene, hudPost);
        SceneTweaks tweaks{};
        tweaks.forceLod = static_cast<int32_t>(slideAt(hud, id.lodf, -1.0f));
        tweaks.gtaoRadius = slideAt(hud, id.gtaor, 2.5f);
        tweaks.gtaoSteps = slideAt(hud, id.gtaoSt, 6.0f);
        tweaks.gtaoSlices = slideAt(hud, id.gtaoSl, 6.0f);
        tweaks.gtaoPower = slideAt(hud, id.gtaoPw, 1.6f);
        tweaks.gtaoFalloff = slideAt(hud, id.gtaoFa, 1.0f);
        tweaks.sscsReach = slideAt(hud, id.sscsR, 0.2f);
        tweaks.sscsThick = slideAt(hud, id.sscsT, 0.02f);
        tweaks.sscsSteps = slideAt(hud, id.sscsS, 12.0f);
        tweaks.sscsPx = slideAt(hud, id.sscsP, 128.0f);
        tweaks.rcRays = slideAt(hud, id.rcRays, 1.0f);
        tweaks.rcSpacing = slideAt(hud, id.rcCell, 0.45f);
        tweaks.rcIntensity = slideAt(hud, id.rcGain, 1.0f);
        tweaks.giGain = slideAt(hud, id.giGain, 2.0f);
        tweaks.rcMax = slideAt(hud, id.rcMax, 48.0f);
        tweaks.bloomThreshold = slideAt(hud, id.bloomth, 0.35f);
        tweaks.fogDensity = slideAt(hud, id.fog, 0.002f);
        tweaks.fogHeight = slideAt(hud, id.fogH, 4.0f);
        tweaks.fogScatter = slideAt(hud, id.fogS, 1.0f);
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
        if (checkOn(hud, id.cmaa, true)) {
            tweaks.passMask |= 8u;
        }
        if (checkOn(hud, id.gtaoen, true)) {
            tweaks.passMask |= 16u;
        }
        if (checkOn(hud, id.bloomen, true)) {
            tweaks.passMask |= 32u;
        }
        if (checkOn(hud, id.smaa, false)) {
            tweaks.passMask |= 4u;
        }
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
        for (uint32_t i = 0; i < 3; ++i) {
            if (checkOn(hud, id.prof[i], false)) {
                tweaks.lightProfile = i;
            }
        }
        if (checkOn(hud, id.rcen, true)) {
            tweaks.passMask |= 128u;
        }
        tweaks.tonemapper = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            if (checkOn(hud, id.tone[i], false)) {
                tweaks.tonemapper = i;
            }
        }
        sceneFrameTweaks(e.scene, tweaks);
        sceneFrameKick(e.scene, e.host.device);
        const char* gpuText = sceneFrameGpuText(e.scene);
        char lines[12][96]{};
        uint32_t line = 0;
        const char* cursor = gpuText != nullptr ? gpuText : "";
        while (line < 12 && cursor[0] != '\0') {
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
            for (uint32_t i = 0; i < 12; ++i) {
                uiText(*hud.state, id.stat[i], lines[i]);
            }
        }
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok;
}

} // namespace burnhope
