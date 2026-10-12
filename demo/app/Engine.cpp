#include "app/Engine.hpp"
#include "asset/AssimpImporter.hpp"

#include <sys/stat.h>
#include "app/Shell.hpp"
#include "render/SceneFrame.hpp"
#include "rhi/Device.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Domain.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/State.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "gpu_scene/SceneObject.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace burnhope {
namespace {

void onColorClose(Host&, int, void* user) {
    auto* colors = static_cast<ColorBook*>(user);
    colors->present = 0;
    ++colors->gen;
}

void openNamed(Host& host, const ViewDesc& desc) {
    (void)hostOpen(host, desc);
}

void onShellOps(Host& host, int slot, const WindowOps& ops, void* user) {
    (void)ops;
    auto* colors = static_cast<ColorBook*>(user);
    const bool wantTool = slot >= 0 && slot < kHostSlots && host.view[slot].wantTool;
    const bool wantColor = slot >= 0 && slot < kHostSlots && host.view[slot].wantColor;
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
        desc.user = colors;
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
        desc.book = colors;
        desc.onOps = onShellOps;
        desc.onClose = onColorClose;
        desc.user = colors;
        openNamed(host, desc);
    }
}

bool sceneTick(void* user, const InputFrame& input, float dt) {
    return sceneFrameTick(static_cast<SceneFrame*>(user), input, dt);
}

void sceneRecord(void* user, VkCommandBuffer cmd, Device& device, DescriptorHeaps& heaps, const Swapchain& swap, const FrameContext& frame) {
    sceneFrameRecord(static_cast<SceneFrame*>(user), cmd, device, heaps, swap, frame);
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
        const char* cache = "cache/bistro-gltf.bhop";
        if (!sceneFrameLoadBhop(e.scene, cache)) {
            mkdir("cache", 0755);
            const char* parts[] = {
                "testscene/assets/bistro/BistroExterior.gltf",
            };
            if (sceneImportFbxList(parts, 1, cache)) {
                (void)sceneFrameLoadBhop(e.scene, cache);
            }
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
        main.sceneTick = sceneTick;
        main.sceneRecord = sceneRecord;
        main.user = e.scene;
        e.host.releaseGpu = releaseScene;
        e.host.releaseUser = e.scene;
        return hostOpen(e.host, main) >= 0;
    }
    ViewDesc main{};
    main.name = "main";
    main.window.title = "Burnhope";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = false;
    main.window.resizable = true;
    main.frame.present = Present::Mailbox;
    main.builder = shellBuild;
    main.book = &e.colors;
    main.onOps = onShellOps;
    main.user = &e.colors;
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

struct PageHit {
    Canvas* canvas = nullptr;
    uint16_t page = kUiNone;
};

struct StatsHit {
    Canvas* canvas = nullptr;
    uint16_t page = kUiNone;
    uint16_t dock = kUiNone;
    uint8_t open = 0;
};

struct ModeHit {
    Canvas* canvas = nullptr;
    int index = 0;
    uint16_t label = kUiNone;
};

PageHit gPage[8]{};
StatsHit gStats{};
ModeHit gMode[15]{};

struct ToolHit {
    Canvas* canvas = nullptr;
    SceneFrame* scene = nullptr;
    uint16_t objects = kUiNone;
    uint16_t postPage = kUiNone;
    uint16_t sunPage = kUiNone;
};

struct GizmoHit {
    ToolHit* tool = nullptr;
    uint8_t mode = 0;
};

struct ObjectHit {
    Canvas* canvas = nullptr;
    uint8_t index = 0;
    uint16_t page = kUiNone;
};

ToolHit gTool{};
GizmoHit gGizmo[3]{};
ObjectHit gObject[16]{};
uint8_t gShown = 0xff;

void onActor(void* user, uint16_t, int16_t) {
    auto* hit = static_cast<ObjectHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || gTool.scene == nullptr) {
        return;
    }
    sceneFrameActorSelect(gTool.scene, hit->index);
    if (hit->page != kUiNone) {
        uiShowOnly(*hit->canvas->state, hit->page);
    }
    gShown = hit->index;
}

void addActorButton(Canvas& canvas, uint16_t objects, uint8_t index, const char* name, uint8_t kind) {
    if (index >= 16 || objects == kUiNone) {
        return;
    }
    const UiLook look = uiLookInk();
    Panel rowBtn = canvas.state != nullptr ? Panel::at(*canvas.state, objects).button(name, -1, look.button[0], look.button[1], look.button[2], 0) : Panel{};
    uiSetHeight(*canvas.state, rowBtn.id, 28.0f);
    gObject[index].canvas = &canvas;
    gObject[index].index = index;
    gObject[index].page = kind == static_cast<uint8_t>(SceneActorKind::Sun) ? gTool.sunPage : gTool.postPage;
    uiBind(*canvas.state, rowBtn.id, onActor, &gObject[index]);
}

void onGizmoMode(void* user, uint16_t, int16_t) {
    auto* hit = static_cast<GizmoHit*>(user);
    if (hit == nullptr || hit->tool == nullptr || hit->tool->scene == nullptr) {
        return;
    }
    sceneFrameActorGizmo(hit->tool->scene, hit->mode);
}

void onDuplicate(void* user, uint16_t, int16_t) {
    auto* tool = static_cast<ToolHit*>(user);
    if (tool == nullptr || tool->scene == nullptr || tool->canvas == nullptr) {
        return;
    }
    const uint8_t index = sceneFrameActorDuplicate(tool->scene);
    if (index == kSceneActorNone) {
        return;
    }
    char name[24];
    uint8_t kind = 0;
    sceneFrameActorName(tool->scene, index, name, sizeof(name), &kind);
    addActorButton(*tool->canvas, tool->objects, index, name, kind);
    onActor(&gObject[index], 0, 0);
}

void setSlide(Canvas& canvas, const char* name, float value) {
    if (canvas.state == nullptr) {
        return;
    }
    const uint16_t id = uiFindName(*canvas.state, name);
    if (id == kUiNone || id >= canvas.state->count) {
        return;
    }
    auto* range = canvas.state->ent[id].try_get_mut<UiRange>();
    if (range == nullptr || range->value == value) {
        return;
    }
    range->value = value;
    canvas.state->visualDirty = 1;
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

void onObjectPage(void* user, uint16_t, int16_t) {
    auto* hit = static_cast<PageHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->page == kUiNone) {
        return;
    }
    uiShowOnly(*hit->canvas->state, hit->page);
}

void onStats(void* user, uint16_t, int16_t) {
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
            Panel::at(*hit->canvas->state, hit->label).setText(title);
        }
    }
}

Panel foldOpen(Panel parent, const char* title) {
    if (parent.state == nullptr) {
        return {};
    }
    UiState& s = *parent.state;
    const uint16_t body = uiTreeRow(s, parent.id, title);
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* tree = s.ent[id].try_get_mut<UiTree>();
        if (tree != nullptr && tree->body == body) {
            tree->open = 1;
            break;
        }
    }
    uiShow(s, body, true);
    return Panel::at(*parent.state, body);
}

Panel rowSlide(Panel panel, const char* text, const char* name, float value, float minV, float maxV) {
    Panel lab = panel.label(text);
    (void)lab;
    Panel slide = panel.slider(value, minV, maxV);
    slide.name(name);
    if (panel.state != nullptr && slide.id < panel.state->count) {
        if (auto* flex = panel.state->ent[slide.id].try_get_mut<UiFlex>()) {
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
            flex->height = 28.0f;
            panel.state->layoutDirty = true;
            panel.state->visualDirty = true;
        }
    }
    return slide;
}

bool fontBesideExe(Canvas& canvas) {
    char exe[512]{};
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        return false;
    }
    exe[n] = '\0';
    char dir[512]{};
    std::snprintf(dir, sizeof(dir), "%s", exe);
    for (int up = 0; up < 5; ++up) {
        char* slash = std::strrchr(dir, '/');
        if (slash == nullptr) {
            break;
        }
        *slash = '\0';
        char face[640]{};
        std::snprintf(face, sizeof(face), "%s/assets/fonts/Inter-Regular.ttf", dir);
        if (canvasSetFont(canvas, face, 20.0f)) {
            return true;
        }
    }
    return false;
}

void sceneHud(Canvas& canvas, void* user) {
    const bool haveFont = canvas.state != nullptr && canvas.state->font.ready;
    if ((!haveFont && !fontBesideExe(canvas) && !canvasUseFont(canvas, 20.0f)) || canvas.state == nullptr) {
        return;
    }
    canvasSetLook(canvas, uiLookInk());
    const UiLook look = canvasLook(canvas);
    auto hiddenPaint = []() {
        UiPaint paint{};
        paint.a = 0.0f;
        paint.role = static_cast<uint8_t>(UiRole::Hidden);
        return paint;
    };
    auto dockPaint = [&]() {
        UiPaint paint{};
        paint.role = static_cast<uint8_t>(UiRole::Panel);
        paint.r = look.content[0];
        paint.g = look.content[1];
        paint.b = look.content[2];
        paint.a = 0.94f;
        paint.borderW = 1.0f;
        paint.br = 0.04f;
        paint.bg = 0.04f;
        paint.bb = 0.04f;
        return paint;
    };
    auto splitBar = [&](Panel parent, bool vertical) {
        UiFlex grip{};
        grip.shrink = 0.0f;
        if (vertical) {
            grip.widthMode = static_cast<uint8_t>(UiSize::Px);
            grip.width = 8.0f;
            grip.heightMode = static_cast<uint8_t>(UiSize::Percent);
            grip.height = 100.0f;
        } else {
            grip.heightMode = static_cast<uint8_t>(UiSize::Px);
            grip.height = 8.0f;
            grip.widthMode = static_cast<uint8_t>(UiSize::Percent);
            grip.width = 100.0f;
        }
        UiPaint paint{};
        paint.role = static_cast<uint8_t>(UiRole::Panel);
        paint.tag = -5;
        paint.a = 1.0f;
        paint.r = 0.05f;
        paint.g = 0.05f;
        paint.b = 0.06f;
        return parent.child(grip, paint);
    };
    auto scrollPage = [&](Panel parent, bool show) {
        UiFlex viewFlex{};
        viewFlex.grow = 1.0f;
        viewFlex.shrink = 1.0f;
        viewFlex.minH = 36.0f;
        UiPaint scroll{};
        scroll.role = static_cast<uint8_t>(UiRole::Scroll);
        scroll.r = look.content[0];
        scroll.g = look.content[1];
        scroll.b = look.content[2];
        scroll.a = 1.0f;
        Panel view = parent.child(viewFlex, scroll);
        UiFlex column{};
        column.gap = 4.0f;
        column.pad = 8.0f;
        column.shrink = 0.0f;
        Panel inner = view.child(column, hiddenPaint());
        if (canvas.state != nullptr) {
            uiBindScroll(*canvas.state, view.id, inner.id);
        }
        if (!show) {
            uiShow(*canvas.state, view.id, false);
        }
        return inner;
    };
    UiFlex rootFlex{};
    rootFlex.grow = 1.0f;
    rootFlex.direction = 0;
    rootFlex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    rootFlex.width = 100.0f;
    rootFlex.heightMode = static_cast<uint8_t>(UiSize::Percent);
    rootFlex.height = 100.0f;
    Panel root = canvas.state != nullptr ? Panel::make(*canvas.state, kUiNone, rootFlex, hiddenPaint()) : Panel{};
    UiFlex barFlex{};
    barFlex.direction = 1;
    barFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    barFlex.height = 50.0f;
    barFlex.minH = 50.0f;
    barFlex.shrink = 0.0f;
    barFlex.align = 1;
    barFlex.pad = 12.0f;
    barFlex.gap = 8.0f;
    Panel top = root.child(barFlex, dockPaint());
    Panel fileBtn = top.button("File", -7, look.button[0], look.button[1], look.button[2], 0);
    uiSetHeight(*canvas.state, fileBtn.id, 28.0f);
    uint16_t fileMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state) canvas.state->contextOf[fileBtn.id] = fileMenu;
    auto fileAct = [](void* user) {
        auto* hit = static_cast<int*>(user);
        if (hit == nullptr || gTool.canvas == nullptr || gTool.canvas->state == nullptr) {
            return;
        }
        char path[512]{};
        const char* home = std::getenv("HOME");
        std::snprintf(path, sizeof(path), "%s/BurnhopeProjects/Untitled", home != nullptr ? home : ".");
        if (*hit == 0) {
            filesNewProject(*gTool.canvas->state, path);
        } else if (*hit == 1) {
            filesOpenProject(*gTool.canvas->state, path);
        } else if (*hit == 3) {
            std::exit(0);
        }
    };
    static int fileNew = 0;
    static int fileOpen = 1;
    static int fileSave = 2;
    static int fileExit = 3;
    if (canvas.state != nullptr) {
        (void)uiCommandAddTo(*canvas.state, fileMenu, "New Project...", fileAct, &fileNew);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Open Project...", fileAct, &fileOpen);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Save Scene", fileAct, &fileSave);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Exit", fileAct, &fileExit);
    }
    Panel viewBtn = top.button("Lit", -7, look.button[0], look.button[1], look.button[2], 0);
    uiSetHeight(*canvas.state, viewBtn.id, 28.0f);
    uint16_t viewMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state) canvas.state->contextOf[viewBtn.id] = viewMenu;
    for (int i = 0; i < static_cast<int>(sizeof(kViews) / sizeof(kViews[0])); ++i) {
        gMode[i].canvas = &canvas;
        gMode[i].index = kViews[i].index;
        gMode[i].label = viewBtn.id;
        if (canvas.state != nullptr) {
            (void)uiCommandAddTo(*canvas.state, viewMenu, kViews[i].name, onMode, &gMode[i]);
        }
    }
    Panel statsBtn = top.button("Stats", -1, look.button[0], look.button[1], look.button[2], 0);
    uiSetHeight(*canvas.state, statsBtn.id, 28.0f);
    gTool.canvas = &canvas;
    gTool.scene = static_cast<SceneFrame*>(user);
    const char* toolName[] = {"Move", "Rotate", "Scale"};
    for (uint8_t i = 0; i < 3; ++i) {
        Panel toolBtn = top.button(toolName[i], -1, look.button[0], look.button[1], look.button[2], 0);
        uiSetHeight(*canvas.state, toolBtn.id, 28.0f);
        gGizmo[i].tool = &gTool;
        gGizmo[i].mode = i;
        uiBind(*canvas.state, toolBtn.id, onGizmoMode, &gGizmo[i]);
    }
    Panel dupBtn = top.button("Duplicate", -1, look.button[0], look.button[1], look.button[2], 0);
    uiSetHeight(*canvas.state, dupBtn.id, 28.0f);
    uiBind(*canvas.state, dupBtn.id, onDuplicate, &gTool);
    UiFlex rowFlex{};
    rowFlex.direction = 1;
    rowFlex.grow = 1.0f;
    rowFlex.shrink = 1.0f;
    rowFlex.minH = 72.0f;
    Panel row = root.child(rowFlex, hiddenPaint());
    UiFlex viewFlex{};
    viewFlex.grow = 1.0f;
    viewFlex.shrink = 1.0f;
    (void)row.child(viewFlex, hiddenPaint());
    (void)splitBar(row, true);
    UiFlex dockFlex{};
    dockFlex.direction = 1;
    dockFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    dockFlex.width = 300.0f;
    dockFlex.shrink = 0.0f;
    dockFlex.heightMode = static_cast<uint8_t>(UiSize::Percent);
    dockFlex.height = 100.0f;
    dockFlex.minH = 72.0f;
    Panel dock = row.child(dockFlex, hiddenPaint());
    UiFlex statsFlex{};
    statsFlex.direction = 0;
    statsFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    statsFlex.width = 220.0f;
    statsFlex.shrink = 0.0f;
    statsFlex.minW = 180.0f;
    Panel statsPane = dock.child(statsFlex, dockPaint());
    uiShow(*canvas.state, statsPane.id, false);
    UiFlex statsCol{};
    statsCol.gap = 4.0f;
    statsCol.pad = 8.0f;
    statsCol.shrink = 0.0f;
    Panel stats = statsPane.child(statsCol, hiddenPaint());
    gStats.canvas = &canvas;
    gStats.page = statsPane.id;
    gStats.dock = dock.id;
    gStats.open = 0;
    uiBind(*canvas.state, statsBtn.id, onStats, &gStats);
    UiFlex sideFlex{};
    sideFlex.direction = 0;
    sideFlex.grow = 1.0f;
    sideFlex.shrink = 1.0f;
    sideFlex.minW = 180.0f;
    sideFlex.minH = 72.0f;
    Panel side = dock.child(sideFlex, hiddenPaint());
    UiFlex hierFlex{};
    hierFlex.grow = 1.0f;
    hierFlex.shrink = 1.0f;
    hierFlex.minH = 40.0f;
    UiPaint hierScroll{};
    hierScroll.role = static_cast<uint8_t>(UiRole::Scroll);
    hierScroll.r = look.content[0];
    hierScroll.g = look.content[1];
    hierScroll.b = look.content[2];
    hierScroll.a = 1.0f;
    Panel hierView = side.child(hierFlex, hierScroll);
    UiFlex hierCol{};
    hierCol.gap = 4.0f;
    hierCol.pad = 8.0f;
    hierCol.shrink = 0.0f;
    Panel objects = hierView.child(hierCol, hiddenPaint());
    if (canvas.state != nullptr) {
        uiBindScroll(*canvas.state, hierView.id, objects.id);
    }
    objects.label("Hierarchy").color(look.accent[0][0], look.accent[0][1], look.accent[0][2], 1.0f);
    gTool.objects = objects.id;
    (void)splitBar(side, false);
    UiFlex inspFlex{};
    inspFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    inspFlex.height = 280.0f;
    inspFlex.minH = 40.0f;
    inspFlex.shrink = 1.0f;
    Panel insp = side.child(inspFlex, hiddenPaint());
    Panel post = scrollPage(insp, true);
    Panel sun = scrollPage(insp, false);
    Panel light = scrollPage(insp, false);
    Panel sky = scrollPage(insp, false);
    Panel cull = scrollPage(insp, false);
    auto pick = [&](const char* title, int slot) {
        Panel rowBtn = objects.button(title, -1, look.button[0], look.button[1], look.button[2], 0);
        uiSetHeight(*canvas.state, rowBtn.id, 28.0f);
        gPage[slot].canvas = &canvas;
        const uint16_t inner = slot == 0 ? post.id : slot == 1 ? sun.id : slot == 2 ? light.id : slot == 3 ? sky.id : cull.id;
        gPage[slot].page = canvas.state->parentOf[inner];
        uiBind(*canvas.state, rowBtn.id, onObjectPage, &gPage[slot]);
    };
    gTool.postPage = canvas.state->parentOf[post.id];
    gTool.sunPage = canvas.state->parentOf[sun.id];
    if (gTool.scene != nullptr) {
        const uint8_t count = sceneFrameActorCount(gTool.scene);
        for (uint8_t i = 0; i < count && i < 16; ++i) {
            char name[24];
            uint8_t kind = 0;
            sceneFrameActorName(gTool.scene, i, name, sizeof(name), &kind);
            addActorButton(canvas, objects.id, i, name, kind);
        }
    }
    pick("Light", 2);
    pick("Sky", 3);
    pick("Cull", 4);
    uiShowOnly(*canvas.state, canvas.state->parentOf[post.id]);
    Panel fogBody = foldOpen(post, "Туман");
    Panel postBody = foldOpen(post, "Пост");
    Panel giBody = foldOpen(post, "GI");
    Panel sunBody = foldOpen(sun, "Солнце");
    (void)splitBar(root, false);
    UiFlex fileFlex{};
    fileFlex.direction = 0;
    fileFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    fileFlex.height = 200.0f;
    fileFlex.minH = 120.0f;
    fileFlex.shrink = 1.0f;
    Panel files = root.child(fileFlex, dockPaint());
    if (uiFiles(*canvas.state).files != nullptr) {
        filesShutdown(*canvas.state);
    }
    uiFiles(*canvas.state).fileFrame = files.id;
    filesAttach(*canvas.state);
    filesMount(*canvas.state);
    filesShow(*canvas.state, true);
    UiFlex modeFlex{};
    modeFlex.shrink = 0.0f;
    Panel modeHost = root.child(modeFlex, hiddenPaint());
    uiShow(*canvas.state, modeHost.id, false);
    Panel modes = modeHost;
    Panel panel = stats;
    auto head = [&](const char* text, const char* id) {
        Panel lab = panel.label(text);
        if (id != nullptr) {
            lab.name(id);
        }
        lab.color(look.accent[0][0], look.accent[0][1], look.accent[0][2], 1.0f);
    };
    panel.label("fps").name("stats");
    panel.label("mem").name("mem");
    panel.label("gpu").name("load");
    auto tick = [&](const char* label, const char* id, bool on) {
        Panel box = panel.check(label);
        box.name(id);
        if (on && panel.state != nullptr) {
            uiBindCheck(*panel.state, box.id, 0, 0, true);
        }
    };
    panel = cull;
    head("кулинг", "h1");
    panel.check("Freeze Frustum").name("freeze");
    tick("конус", "cone", true);
    tick("Hi-Z", "hiz", true);
    tick("LOD", "lod", true);
    panel.label("Force LOD  -1..3").name("lodlab");
    panel.spin(-1.0f, -1.0f, 3.0f, 1.0f).name("lodf");
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
    rowSlide(panel, "GI яркость", "giGain", 2.0f, 0.0f, 6.0f);
    panel = giBody;
    head("профиль света", "h3");
    panel.radio("Низкий", 3).name("prof0");
    panel.radio("Баланс", 3).name("prof1");
    panel.radio("Ультра", 3).name("prof2");
    tick("radiance", "rcen", true);
    rowSlide(panel, "RC лучи", "rcRays", 8.0f, 1.0f, 32.0f);
    rowSlide(panel, "RC клетка м", "rcCell", 0.45f, 0.1f, 4.0f);
    rowSlide(panel, "RC сила", "rcGain", 1.0f, 0.0f, 4.0f);
    rowSlide(panel, "RC длина м", "rcMax", 48.0f, 2.0f, 200.0f);
    panel = postBody;
    tick("GTAO", "gtaoen", true);
    tick("CMAA", "cmaa", true);
    panel.check("SMAA").name("smaa");
    tick("Bloom", "bloomen", true);
    panel = sunBody;
    tick("солнце", "sun", true);
    rowSlide(panel, "солнце", "sunI", 1.0f, 0.0f, 4.0f);
    rowSlide(panel, "солнце R", "sunR", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "солнце G", "sunG", 0.97f, 0.0f, 1.0f);
    rowSlide(panel, "солнце B", "sunB", 0.92f, 0.0f, 1.0f);
    rowSlide(panel, "солнце зенит", "zenith", 35.0f, -10.0f, 80.0f);
    rowSlide(panel, "солнце азимут", "azimuth", 30.0f, 0.0f, 360.0f);
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
    panel.check("вирт. текстуры").name("vt");
    panel = modes;
    panel.radio("Lit", 1).name("mode0");
    panel.radio("Normals", 1).name("mode2");
    panel.radio("Primitive ID", 1).name("mode1");
    panel.radio("GI", 1).name("mode4");
    panel.radio("каскады", 1).name("mode5");
    panel.radio("контакт", 1).name("mode6");
    panel.radio("луч", 1).name("mode7");
    panel.radio("прямой", 1).name("mode8");
    panel.radio("альбедо", 1).name("mode9");
    panel.radio("солнце", 1).name("mode10");
    panel.radio("шероховатость", 1).name("mode11");
    panel.radio("металл", 1).name("mode12");
    panel.radio("AO", 1).name("mode13");
    panel.radio("свечение", 1).name("mode14");
    panel = postBody;
    panel.radio("AgX", 2).name("tone0");
    panel.radio("ACES", 2).name("tone1");
    panel.radio("Reinhard", 2).name("tone2");
    panel.radio("Linear", 2).name("tone3");
    rowSlide(panel, "яркость", "bright", 1.0f, 0.05f, 4.0f);
    rowSlide(panel, "зерно", "grain", 0.0f, 0.0f, 0.12f);
    rowSlide(panel, "блюм", "bloom", 0.15f, 0.0f, 0.80f);
    rowSlide(panel, "порог блюма", "bloomth", 0.35f, 0.0f, 2.0f);
    rowSlide(panel, "виньетка", "vig", 0.35f, 0.0f, 2.0f);
    rowSlide(panel, "хромат. пкс", "ca", 2.0f, 0.0f, 4.0f);
    rowSlide(panel, "SSCS", "contact", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "SSCS длина м", "sscsR", 0.2f, 0.05f, 2.0f);
    rowSlide(panel, "SSCS толщина", "sscsT", 0.02f, 0.005f, 0.2f);
    rowSlide(panel, "SSCS шаги", "sscsS", 12.0f, 4.0f, 16.0f);
    rowSlide(panel, "SSCS экран px", "sscsP", 128.0f, 4.0f, 256.0f);
    rowSlide(panel, "окклюзия блика", "spec", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "Toksvig", "tok", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "параллакс", "pom", 0.35f, 0.0f, 1.0f);
    rowSlide(panel, "multi-scatter", "ggx", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "радиус GTAO", "gtaor", 2.5f, 0.2f, 20.0f);
    rowSlide(panel, "GTAO срезы", "gtaoSl", 6.0f, 1.0f, 8.0f);
    rowSlide(panel, "GTAO шаги", "gtaoSt", 6.0f, 1.0f, 12.0f);
    rowSlide(panel, "GTAO сила", "gtaoPw", 1.6f, 0.2f, 4.0f);
    rowSlide(panel, "GTAO край", "gtaoFa", 1.0f, 0.1f, 1.0f);
    rowSlide(panel, "GTAO", "gtao", 1.2f, 0.0f, 4.0f);
    panel = light;
    rowSlide(panel, "свечи", "lamp", 1.0f, 0.0f, 1.0f);
    panel = sky;
    rowSlide(panel, "облака", "clouds", 0.42f, 0.0f, 0.95f);
    rowSlide(panel, "плотность", "cdens", 0.08f, 0.0f, 0.3f);
    panel = fogBody;
    rowSlide(panel, "туман", "fog", 0.002f, 0.0f, 0.08f);
    rowSlide(panel, "высота тумана", "fogH", 4.0f, -30.0f, 40.0f);
    rowSlide(panel, "свет в тумане", "fogS", 1.0f, 0.0f, 2.0f);
    panel = stats;
    for (int i = 0; i < 12; ++i) {
        char name[8];
        std::snprintf(name, sizeof(name), "g%d", i);
        panel.label("").name(name);
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
    }
}

int readFile(const char* path, char* dst, int cap) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0 || cap <= 1) {
        return 0;
    }
    const int n = static_cast<int>(read(fd, dst, static_cast<size_t>(cap - 1)));
    close(fd);
    if (n <= 0) {
        return 0;
    }
    dst[n] = '\0';
    return n;
}

float slideAt(const Canvas& canvas, const char* name, float fallback) {
    float value = fallback;
    float minV = 0.0f;
    float maxV = 1.0f;
    if (!uiRange(*canvas.state, uiFindName(*canvas.state, name), value, minV, maxV)) {
        return fallback;
    }
    return value;
}

bool checkOn(const Canvas& canvas, const char* name, bool fallback) {
    const int on = canvas.state != nullptr ? uiCheckOn(*canvas.state, name) : -1;
    return on < 0 ? fallback : on != 0;
}

void hostLoad(float& cpu, float& gpu) {
    char buf[256];
    static uint64_t prevIdle = 0;
    static uint64_t prevTotal = 0;
    if (readFile("/proc/stat", buf, sizeof(buf)) > 0) {
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, soft = 0;
        if (std::sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &soft) == 7) {
            const uint64_t total = user + nice + system + idle + iowait + irq + soft;
            const uint64_t idleNow = idle + iowait;
            if (prevTotal != 0 && total > prevTotal) {
                cpu = 100.0f * static_cast<float>((total - prevTotal) - (idleNow - prevIdle)) / static_cast<float>(total - prevTotal);
            }
            prevIdle = idleNow;
            prevTotal = total;
        }
    }
    gpu = -1.0f;
    if (readFile("/sys/class/drm/card1/device/gpu_busy_percent", buf, sizeof(buf)) > 0
        || readFile("/sys/class/drm/card0/device/gpu_busy_percent", buf, sizeof(buf)) > 0) {
        gpu = static_cast<float>(std::atoi(buf));
    }
}

bool engineTick(Engine& e) {
    const bool ok = hostTick(e.host);
    if (e.scene != nullptr && e.host.primary >= 0) {
        char fps[96];
        sceneFrameStats(e.scene, fps, sizeof(fps));
        char mem[64];
        uint64_t rss = 0;
        if (readFile("/proc/self/statm", mem, sizeof(mem)) > 0) {
            unsigned long pages = 0;
            unsigned long resident = 0;
            if (std::sscanf(mem, "%lu %lu", &pages, &resident) == 2) {
                rss = static_cast<uint64_t>(resident) * 4096ull;
            }
        }
        float cpu = 0.0f;
        float gpu = -1.0f;
        hostLoad(cpu, gpu);
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
            Panel::find(hudState, "stats").setText(fps);
            Panel::find(hudState, "mem").setText(memLine);
            Panel::find(hudState, "load").setText(loadLine);
        }
        Canvas& hud = e.host.view[e.host.primary].canvas;
        const uint8_t selected = sceneFrameActorSelected(e.scene);
        if (selected != gShown && selected < 16) {
            gShown = selected;
            const uint8_t kind = sceneFrameActorKind(e.scene, selected);
            const uint16_t page = kind == static_cast<uint8_t>(SceneActorKind::Sun) ? gTool.sunPage : gTool.postPage;
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
        uint32_t mode = 0;
        for (uint32_t i = 0; i < 15; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "mode%u", i);
            if (hud.state != nullptr && uiCheckOn(*hud.state, name) == 1) {
                mode = i;
            }
        }
        sceneFrameDebug(e.scene,
            checkOn(hud, "hiz", true), checkOn(hud, "cone", true),
            checkOn(hud, "lod", true), checkOn(hud, "frust", true), checkOn(hud, "small", true), checkOn(hud, "shadow", true),
            mode);
        HudPost hudPost{};
        hudPost.brightness = slideAt(hud, "bright", 1.0f);
        hudPost.grain = slideAt(hud, "grain", hudPost.grain);
        hudPost.bloom = slideAt(hud, "bloom", hudPost.bloom);
        hudPost.vignette = slideAt(hud, "vig", hudPost.vignette);
        hudPost.ca = slideAt(hud, "ca", hudPost.ca);
        hudPost.contact = slideAt(hud, "contact", hudPost.contact);
        hudPost.specOcc = slideAt(hud, "spec", hudPost.specOcc);
        hudPost.toksvig = slideAt(hud, "tok", hudPost.toksvig);
        hudPost.pom = slideAt(hud, "pom", hudPost.pom);
        hudPost.ggx = slideAt(hud, "ggx", hudPost.ggx);
        hudPost.gtao = slideAt(hud, "gtao", hudPost.gtao);
        hudPost.lamp = slideAt(hud, "lamp", hudPost.lamp);
        hudPost.sunGain = slideAt(hud, "sunI", 1.0f);
        hudPost.sunR = slideAt(hud, "sunR", 1.0f);
        hudPost.sunG = slideAt(hud, "sunG", 0.97f);
        hudPost.sunB = slideAt(hud, "sunB", 0.92f);
        sceneFrameTune(e.scene, hudPost);
        SceneTweaks tweaks{};
        tweaks.forceLod = static_cast<int32_t>(slideAt(hud, "lodf", -1.0f));
        tweaks.gtaoRadius = slideAt(hud, "gtaor", 2.5f);
        tweaks.gtaoSteps = slideAt(hud, "gtaoSt", 6.0f);
        tweaks.gtaoSlices = slideAt(hud, "gtaoSl", 6.0f);
        tweaks.gtaoPower = slideAt(hud, "gtaoPw", 1.6f);
        tweaks.gtaoFalloff = slideAt(hud, "gtaoFa", 1.0f);
        tweaks.sscsReach = slideAt(hud, "sscsR", 0.2f);
        tweaks.sscsThick = slideAt(hud, "sscsT", 0.02f);
        tweaks.sscsSteps = slideAt(hud, "sscsS", 12.0f);
        tweaks.sscsPx = slideAt(hud, "sscsP", 128.0f);
        tweaks.rcRays = slideAt(hud, "rcRays", 8.0f);
        tweaks.rcSpacing = slideAt(hud, "rcCell", 0.45f);
        tweaks.rcIntensity = slideAt(hud, "rcGain", 1.0f);
        tweaks.giGain = slideAt(hud, "giGain", 2.0f);
        tweaks.rcMax = slideAt(hud, "rcMax", 48.0f);
        tweaks.bloomThreshold = slideAt(hud, "bloomth", 0.35f);
        tweaks.fogDensity = slideAt(hud, "fog", 0.002f);
        tweaks.fogHeight = slideAt(hud, "fogH", 4.0f);
        tweaks.fogScatter = slideAt(hud, "fogS", 1.0f);
        float zenith = slideAt(hud, "zenith", 35.0f);
        float azimuth = slideAt(hud, "azimuth", 30.0f);
        if (sunIndex != kSceneActorNone) {
            float pitch = zenith;
            float yaw = azimuth;
            sceneFrameActorEuler(e.scene, sunIndex, pitch, yaw);
            const bool dragging = sceneFrameActorDragging(e.scene) && selected == sunIndex;
            if (dragging || (std::fabs(zenith - pitch) <= 0.25f && std::fabs(azimuth - yaw) <= 0.25f)) {
                setSlide(hud, "zenith", pitch);
                setSlide(hud, "azimuth", yaw);
                zenith = pitch;
                azimuth = yaw;
            } else {
                sceneFrameActorSetEuler(e.scene, sunIndex, zenith, azimuth);
            }
        }
        tweaks.sunZenith = zenith;
        tweaks.sunAzimuth = azimuth;
        tweaks.cloudCoverage = slideAt(hud, "clouds", 0.42f);
        tweaks.cloudDensity = slideAt(hud, "cdens", 0.08f);
        tweaks.freeze = checkOn(hud, "freeze", false);
        tweaks.passMask = 0;
        if (checkOn(hud, "cluster", true)) {
            tweaks.passMask |= 1u;
        }
        if (checkOn(hud, "probes", true)) {
            tweaks.passMask |= 2u;
        }
        if (checkOn(hud, "gien", true)) {
            tweaks.passMask |= 262144u;
        }
        if (checkOn(hud, "cmaa", true)) {
            tweaks.passMask |= 8u;
        }
        if (checkOn(hud, "gtaoen", true)) {
            tweaks.passMask |= 16u;
        }
        if (checkOn(hud, "bloomen", true)) {
            tweaks.passMask |= 32u;
        }
        if (checkOn(hud, "smaa", false)) {
            tweaks.passMask |= 4u;
        }
        if (checkOn(hud, "sun", true)) {
            tweaks.passMask |= 256u;
        }
        if (checkOn(hud, "points", true)) {
            tweaks.passMask |= 512u;
        }
        if (checkOn(hud, "spots", true)) {
            tweaks.passMask |= 65536u;
        }
        if (checkOn(hud, "area", true)) {
            tweaks.passMask |= 131072u;
        }
        if (checkOn(hud, "sky", true)) {
            tweaks.passMask |= 1024u;
        }
        if (checkOn(hud, "clouden", true)) {
            tweaks.passMask |= 2048u;
        }
        if (checkOn(hud, "fogen", true)) {
            tweaks.passMask |= 4096u;
        }
        if (checkOn(hud, "reflecten", true)) {
            tweaks.passMask |= 8192u;
        }
        if (checkOn(hud, "contacten", true)) {
            tweaks.passMask |= 16384u;
        }
        if (checkOn(hud, "vt", false)) {
            tweaks.passMask |= 32768u;
        }
        tweaks.lightProfile = 0;
        for (uint32_t i = 0; i < 3; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "prof%u", i);
            if (hud.state != nullptr && uiCheckOn(*hud.state, name) == 1) {
                tweaks.lightProfile = i;
            }
        }
        if (checkOn(hud, "rcen", true)) {
            tweaks.passMask |= 128u;
        }
        tweaks.tonemapper = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "tone%u", i);
            if (hud.state != nullptr && uiCheckOn(*hud.state, name) == 1) {
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
        for (uint32_t i = 0; i < 12; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "g%u", i);
            if (e.host.view[e.host.primary].canvas.state != nullptr) {
                Panel::find(*e.host.view[e.host.primary].canvas.state, name).setText(lines[i]);
            }
        }
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok;
}

} // namespace burnhope
