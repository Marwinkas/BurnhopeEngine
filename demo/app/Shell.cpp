#include "app/Shell.hpp"
#include "files/Browser.hpp"
#include "ui/Panel.hpp"

#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

struct Demo {
    Canvas* canvas = nullptr;
    uint16_t chips = kUiNone;
    uint16_t tools = kUiNone;
    uint16_t settings = kUiNone;
    uint16_t dockHome = kUiNone;
    uint16_t dockPanel = kUiNone;
    uint8_t edges = 15;
    uint16_t scale = kUiNone;
    uint16_t preview = kUiNone;
    uint16_t scaleText = kUiNone;
    bool chipsOn = true;
    bool toolsOn = true;
    bool settingsOn = false;
};

Demo gDemo;

void onScale(void* user, uint16_t id, float value) {
    auto* demo = static_cast<Demo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || id != demo->scale) {
        return;
    }
    float minV = 0.5f;
    float maxV = 2.0f;
    float current = value;
    if (!canvasRange(*demo->canvas, id, current, minV, maxV)) {
        return;
    }
    float t = maxV > minV ? (value - minV) / (maxV - minV) : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    canvasSetHeight(*demo->canvas, demo->preview, 40.0f + t * 260.0f);
    canvasSetPaint(*demo->canvas, demo->preview, 0.16f + t * 0.55f, 0.28f + (1.0f - t) * 0.25f, 0.62f - t * 0.2f, 1.0f);
    char text[16]{};
    std::snprintf(text, sizeof(text), "SCALE %.2f", value);
    canvasText(*demo->canvas, demo->scaleText, text);
}

void onClick(void* user, uint16_t id, int16_t tag) {
    auto* demo = static_cast<Demo*>(user);
    if (demo == nullptr || demo->canvas == nullptr) {
        return;
    }
    Canvas& canvas = *demo->canvas;
    if (tag == -100) {
        filesClick(*canvas.state, id);
        return;
    }
    if (tag == -93) {
        filesShow(*canvas.state, false);
        return;
    }
    if (tag == -94) {
        filesShow(*canvas.state, true);
        return;
    }
    if (tag == -8) {
        demo->chipsOn = !demo->chipsOn;
        canvasShow(canvas, demo->chips, demo->chipsOn);
    } else if (tag == -11) {
        demo->toolsOn = !demo->toolsOn;
        canvasShow(canvas, demo->tools, demo->toolsOn);
    } else if (tag == -9) {
        demo->settingsOn = !demo->settingsOn;
        canvasShow(canvas, demo->settings, demo->settingsOn);
    } else if (tag == -10) {
        canvasCommitDock(canvas, 1);
    } else if (tag == -16) {
        canvasOpenOs(canvas);
    } else if (tag >= -20 && tag <= -17) {
        const uint8_t bit = static_cast<uint8_t>(1 << (-17 - tag));
        uint8_t next = static_cast<uint8_t>(demo->edges ^ bit);
        if (next == 0) {
            next = bit;
        }
        demo->edges = next;
        canvasDock(canvas, demo->dockPanel, demo->dockHome, next);
    } else if (tag == -21) {
        canvasSetDrop(canvas, demo->chips, 1);
    } else if (tag == -22) {
        canvasSetDrop(canvas, demo->chips, 2);
    } else if (tag == -12 || tag == -13) {
        uint16_t id = kUiNone;
        char name[32]{};
        uint8_t len = 0;
        canvasSelection(canvas, id, name, 32, len);
        spdlog::info("app pick id {} name {}", id, name);
    }
}

void ping(void*) {
    spdlog::info("app command: ping");
}

void growCmd(void* user) {
    onClick(user, kUiNone, -8);
}

void warmCmd(void* user) {
    auto* demo = static_cast<Demo*>(user);
    if (demo != nullptr && demo->canvas != nullptr) {
        canvasSetLook(*demo->canvas, uiLookWarm());
    }
}

UiPaint panelPaint(float r, float g, float b) {
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Panel);
    paint.r = r;
    paint.g = g;
    paint.b = b;
    paint.a = 1.0f;
    paint.radius = 8.0f;
    return paint;
}

void onFold(void* user, uint16_t id, int16_t) {
    canvasFold(*static_cast<Canvas*>(user), id);
}

Panel dragHeader(Panel parent, const char* title) {
    UiFlex row{};
    row.direction = 1;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = 28.0f;
    row.widthMode = static_cast<uint8_t>(UiSize::Percent);
    row.width = 100.0f;
    row.shrink = 0.0f;
    row.align = 1;
    row.pad = 8.0f;
    UiPaint paint = panelPaint(0.20f, 0.24f, 0.32f);
    paint.tag = -6;
    paint.radius = 6.0f;
    row.pad = 0.0f;
    row.padL = 8.0f;
    row.padR = 4.0f;
    Panel header = parent.child(row, paint);
    (void)header.label(title);
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.heightMode = static_cast<uint8_t>(UiSize::Px);
    gap.height = 1.0f;
    UiPaint hidden{};
    hidden.role = static_cast<uint8_t>(UiRole::Hidden);
    (void)header.child(gap, hidden);
    UiFlex mark{};
    mark.widthMode = static_cast<uint8_t>(UiSize::Px);
    mark.heightMode = static_cast<uint8_t>(UiSize::Px);
    mark.width = 22.0f;
    mark.height = 22.0f;
    mark.shrink = 0.0f;
    mark.marginR = 2.0f;
    UiPaint fold{};
    fold.role = static_cast<uint8_t>(UiRole::Button);
    fold.r = 0.22f;
    fold.g = 0.26f;
    fold.b = 0.34f;
    fold.a = 1.0f;
    fold.radius = 4.0f;
    fold.tag = -14;
    fold.icon = 1;
    Panel foldBtn = header.child(mark, fold);
    if (parent.canvas != nullptr) {
        canvasBind(*parent.canvas, foldBtn.id, onFold, parent.canvas);
    }
    return header;
}

} // namespace

bool imagePath(const char* path) {
    const char* dot = nullptr;
    for (const char* p = path; p != nullptr && *p != '\0'; ++p) {
        if (*p == '.') {
            dot = p;
        }
    }
    if (dot == nullptr) {
        return false;
    }
    char ext[8]{};
    int n = 0;
    for (const char* p = dot; *p != '\0' && n < 7; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        ext[n++] = c;
    }
    return std::strcmp(ext, ".png") == 0 || std::strcmp(ext, ".jpg") == 0 || std::strcmp(ext, ".jpeg") == 0
        || std::strcmp(ext, ".gif") == 0 || std::strcmp(ext, ".bmp") == 0 || std::strcmp(ext, ".tga") == 0;
}

void onBrowse(void* user, const char* path, int isDir) {
    auto* demo = static_cast<Demo*>(user);
    if (isDir != 0 || demo == nullptr || demo->canvas == nullptr || path == nullptr || !imagePath(path)) {
        return;
    }
    if (canvasLoadImage(*demo->canvas, 0, path)) {
        spdlog::info("browser opened image {}", path);
    }
}

void shellBuild(Canvas& canvas, void*) {
    if (canvas.state == nullptr) {
        return;
    }
    gDemo = {};
    gDemo.canvas = &canvas;
    gDemo.chipsOn = true;
    gDemo.toolsOn = true;
    if (!canvasSetFont(canvas, "/usr/share/fonts/TTF/DejaVuSans.ttf", 22.0f)) {
        spdlog::error("app font was not set");
    }
    canvasBuildSample(canvas);
    filesAttach(*canvas.state);
    filesMount(*canvas.state);
    filesShow(*canvas.state, true);
    gDemo.scale = canvasFind(canvas, "scale");
    gDemo.preview = canvasFind(canvas, "preview");
    gDemo.scaleText = canvasFind(canvas, "scale_text");
    canvasListen(canvas, onScale, &gDemo);
    filesOnOpen(*canvas.state, onBrowse, &gDemo);
    const uint16_t note = canvasFind(canvas, "json_note");
    if (note != kUiNone) {
        canvasText(canvas, note, "named in flecs");
    }
    canvasMenuStyle(canvas, 210.0f, 30.0f, 8.0f, 4.0f);
    canvasSetClick(canvas, onClick, &gDemo);

    Panel look = Panel::at(canvas, canvasPage(canvas, 1));
    UiFlex block{};
    block.direction = 0;
    block.gap = 8.0f;
    block.shrink = 0.0f;
    UiPaint clear{};
    clear.role = static_cast<uint8_t>(UiRole::Hidden);
    Panel section = look.child(block, clear);

    Panel heading = section.textButton("YOGA GROW", -8);
    heading.color(0.82f, 0.90f, 1.0f, 1.0f);

    UiFlex strip{};
    strip.direction = 1;
    strip.heightMode = static_cast<uint8_t>(UiSize::Px);
    strip.height = 44.0f;
    strip.gap = 8.0f;
    strip.shrink = 0.0f;
    strip.drop = 1;
    Panel row = section.child(strip, clear);
    gDemo.chips = row.id;
    const float grow[3] = {1.0f, 2.0f, 1.0f};
    const float chip[3][3] = {
        {0.25f, 0.32f, 0.48f},
        {0.20f, 0.42f, 0.38f},
        {0.45f, 0.32f, 0.22f},
    };
    uint16_t chipId = kUiNone;
    for (int i = 0; i < 3; ++i) {
        UiFlex chipFlex{};
        chipFlex.grow = grow[i];
        chipFlex.shrink = 1.0f;
        chipFlex.heightMode = static_cast<uint8_t>(UiSize::Percent);
        chipFlex.height = 100.0f;
        chipFlex.minW = i == 2 ? 360.0f : 0.0f;
        UiPaint chipPaint = panelPaint(chip[i][0], chip[i][1], chip[i][2]);
        Panel piece = row.child(chipFlex, chipPaint);
        if (i == 0) {
            chipId = piece.id;
        }
        if (i == 1) {
            piece.animate(0.30f, 0.62f, 0.48f, 1.0f);
        }
        if (i == 2) {
            (void)piece.label("MIN 360");
        }
    }

    UiFlex nestFlex{};
    nestFlex.direction = 0;
    nestFlex.pad = 8.0f;
    nestFlex.gap = 4.0f;
    nestFlex.shrink = 0.0f;
    Panel nest = section.child(nestFlex, panelPaint(0.14f, 0.16f, 0.20f));
    UiFlex innerFlex{};
    innerFlex.direction = 0;
    innerFlex.pad = 6.0f;
    innerFlex.shrink = 0.0f;
    Panel inner = nest.child(innerFlex, panelPaint(0.18f, 0.20f, 0.26f));
    (void)inner.label("NESTED LAYER");

    section.textButton("DROP LAST", -21).color(0.86f, 0.90f, 0.96f, 1.0f);
    section.textButton("DROP FIRST", -22).color(0.86f, 0.90f, 0.96f, 1.0f);
    section.textButton("TOOLS", -11).color(0.78f, 0.88f, 0.96f, 1.0f);
    UiFlex toolsFlex{};
    toolsFlex.direction = 1;
    toolsFlex.gap = 8.0f;
    toolsFlex.shrink = 0.0f;
    toolsFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    toolsFlex.height = 36.0f;
    Panel tools = section.child(toolsFlex, clear);
    gDemo.tools = tools.id;
    (void)tools.button("SAVE", -12, 0.18f, 0.28f, 0.42f, 4);
    (void)tools.button("PICK", -13, 0.16f, 0.24f, 0.34f, 6);
    (void)tools.button("", -9, 0.22f, 0.26f, 0.34f, 5);
    (void)tools.button("WINDOW", -16, 0.24f, 0.28f, 0.22f, 4);

    UiFlex homeFlex{};
    homeFlex.direction = 0;
    homeFlex.gap = 6.0f;
    homeFlex.grow = 1.0f;
    homeFlex.shrink = 1.0f;
    homeFlex.minH = 180.0f;
    Panel home = section.child(homeFlex, panelPaint(0.10f, 0.12f, 0.15f));
    gDemo.dockHome = home.id;
    (void)home.label("DOCK");

    UiFlex dockFlex{};
    dockFlex.direction = 0;
    dockFlex.gap = 6.0f;
    dockFlex.pad = 8.0f;
    dockFlex.shrink = 0.0f;
    Panel dock = home.child(dockFlex, panelPaint(0.16f, 0.18f, 0.24f));
    gDemo.dockPanel = dock.id;
    canvasDock(canvas, dock.id, home.id);
    dragHeader(dock, "INSPECTOR");
    (void)dock.label("drag the header out, drop it here or press DOCK");
    (void)dock.button("DOCK", -10, 0.20f, 0.32f, 0.28f, 0);
    (void)dock.textButton("LEFT", -17);
    (void)dock.textButton("RIGHT", -18);
    (void)dock.textButton("TOP", -19);
    (void)dock.textButton("BOTTOM", -20);

    UiFlex win{};
    win.position = 1;
    win.posX = 640.0f;
    win.posY = 160.0f;
    win.widthMode = static_cast<uint8_t>(UiSize::Px);
    win.width = 300.0f;
    win.heightMode = static_cast<uint8_t>(UiSize::Px);
    win.height = 168.0f;
    win.pad = 10.0f;
    win.gap = 8.0f;
    win.shrink = 0.0f;
    UiPaint settingsPaint = panelPaint(0.12f, 0.13f, 0.17f);
    settingsPaint.tag = -15;
    Panel settings = Panel::make(canvas, 0, win, settingsPaint);
    gDemo.settings = settings.id;
    dragHeader(settings, "SETTINGS");
    (void)settings.label("separate window, drag the header");
    (void)settings.button("CLOSE", -9, 0.40f, 0.24f, 0.24f, 3);
    canvasShow(canvas, settings.id, false);

    const uint16_t sectionMenu = canvasMakeMenu(canvas);
    const uint16_t chipMenu = canvasMakeMenu(canvas);
    canvasBindContext(canvas, section.id, sectionMenu);
    canvasBindContext(canvas, chipId, chipMenu);
    if (!canvasAddCommandTo(canvas, sectionMenu, "GROW", growCmd, &gDemo)
        || !canvasAddCommandTo(canvas, sectionMenu, "PING", ping, nullptr)
        || !canvasAddCommandTo(canvas, chipMenu, "WARM", warmCmd, &gDemo)) {
        spdlog::error("context menu was not filled");
    }
    if (!canvasAddCommand(canvas, "PING", ping, nullptr)) {
        spdlog::error("PING command was not added");
    }
}

void toolBuild(Canvas& canvas, void*) {
    if (!canvasSetFont(canvas, "/usr/share/fonts/TTF/DejaVuSans.ttf", 22.0f)) {
        spdlog::error("tool font was not set");
    }
    UiFlex rootFlex{};
    rootFlex.direction = 0;
    rootFlex.pad = 0.0f;
    UiPaint rootPaint = panelPaint(0.10f, 0.11f, 0.13f);
    rootPaint.radius = 0.0f;
    Panel root = Panel::make(canvas, kUiNone, rootFlex, rootPaint);

    UiFlex title{};
    title.direction = 1;
    title.heightMode = static_cast<uint8_t>(UiSize::Px);
    title.height = 40.0f;
    title.align = 1;
    title.shrink = 0.0f;
    title.pad = 10.0f;
    Panel bar = root.child(title, panelPaint(0.16f, 0.18f, 0.22f));
    (void)bar.label("SETTINGS");
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    UiPaint hidden{};
    hidden.role = static_cast<uint8_t>(UiRole::Hidden);
    (void)bar.child(gap, hidden);
    UiFlex closeFlex{};
    closeFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    closeFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    closeFlex.width = 28.0f;
    closeFlex.height = 28.0f;
    closeFlex.shrink = 0.0f;
    UiPaint closePaint{};
    closePaint.role = static_cast<uint8_t>(UiRole::Close);
    closePaint.r = 0.55f;
    closePaint.g = 0.28f;
    closePaint.b = 0.28f;
    closePaint.a = 1.0f;
    closePaint.radius = 6.0f;
    closePaint.icon = 3;
    (void)bar.child(closeFlex, closePaint);

    UiFlex body{};
    body.direction = 0;
    body.pad = 16.0f;
    body.gap = 10.0f;
    body.grow = 1.0f;
    Panel page = root.child(body, hidden);
    (void)page.label("Separate OS window.");
    (void)page.label("Drag the title. Resize any edge.");
    (void)page.button("PING", -12, 0.20f, 0.28f, 0.40f, 6);
}

void colorBuild(Canvas& canvas, void*) {
    if (!canvasSetFont(canvas, "/usr/share/fonts/TTF/DejaVuSans.ttf", 22.0f)) {
        spdlog::error("color font was not set");
    }
    canvasBuildColor(canvas);
}

} // namespace burnhope
