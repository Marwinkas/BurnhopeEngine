#include "app/Shell.hpp"
#include "files/Browser.hpp"
#include "ui/Domain.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Slider.hpp"

#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

namespace burnhope {

void uiBuildShell(UiState& s);
void uiBuildColor(UiState& s);

namespace {

void onScale(void* user, uint16_t id, float value) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || id != demo->scale) {
        return;
    }
    float minV = 0.5f;
    float maxV = 2.0f;
    float current = value;
    if (!uiRange(*demo->canvas->state, id, current, minV, maxV)) {
        return;
    }
    float t = maxV > minV ? (value - minV) / (maxV - minV) : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    uiSetHeight(*demo->canvas->state, demo->preview, 40.0f + t * 260.0f);
    if (demo->canvas->state != nullptr) {
        uiSetPaint(*demo->canvas->state, demo->preview, 0.16f + t * 0.55f, 0.28f + (1.0f - t) * 0.25f, 0.62f - t * 0.2f, 1.0f);
    }
    char text[16]{};
    std::snprintf(text, sizeof(text), "SCALE %.2f", value);
    uiText(*demo->canvas->state, demo->scaleText, text);
}

void toggleShow(ShellDemo& demo, uint16_t id, bool& flag) {
    if (demo.canvas == nullptr || demo.canvas->state == nullptr) {
        return;
    }
    flag = !flag;
    uiShow(*demo.canvas->state, id, flag);
}

void toggleChips(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->chips, demo->chipsOn);
    }
}

void toggleTools(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->tools, demo->toolsOn);
    }
}

void toggleSettings(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->settings, demo->settingsOn);
    }
}

void onDock(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiDockTo(*demo->canvas->state, 1);
    }
}

void onToolWindow(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiHostNote(*demo->canvas->state).tool = true;
    }
}

void setEdge(void* user, uint8_t bit) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || demo->canvas->state == nullptr) {
        return;
    }
    uint8_t next = static_cast<uint8_t>(demo->edges ^ bit);
    if (next == 0) {
        next = bit;
    }
    demo->edges = next;
    uiDock(*demo->canvas->state).panel = demo->dockPanel;
    uiDock(*demo->canvas->state).home = demo->dockHome;
    uiDock(*demo->canvas->state).edges = next;
}

void onEdgeLeft(void* user, uint16_t, int16_t) { setEdge(user, 1); }

void onEdgeRight(void* user, uint16_t, int16_t) { setEdge(user, 2); }

void onEdgeTop(void* user, uint16_t, int16_t) { setEdge(user, 4); }

void onEdgeBottom(void* user, uint16_t, int16_t) { setEdge(user, 8); }

void onDropLast(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiSetDrop(*demo->canvas->state, demo->chips, 1);
    }
}

void onDropFirst(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiSetDrop(*demo->canvas->state, demo->chips, 2);
    }
}

void logPick(void* user, uint16_t, int16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || demo->canvas->state == nullptr) {
        return;
    }
    const UiPick& pick = uiPick(*demo->canvas->state);
    char name[32]{};
    const uint8_t len = pick.len < 31 ? pick.len : 31;
    for (uint8_t i = 0; i < len; ++i) {
        name[i] = pick.name[i];
    }
    name[len] = '\0';
    spdlog::info("app pick id {} name {}", pick.id, name);
}

void ping(void*) {
    spdlog::info("app command: ping");
}

void growCmd(void* user) {
    toggleChips(user, kUiNone, 0);
}

void warmCmd(void* user) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr) {
        canvasSetLook(*demo->canvas, uiLookWarm());
    }
}

void onFold(void* user, uint16_t id, int16_t) {
    uiFold(*static_cast<UiState*>(user), id);
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
    row.pad = 0.0f;
    row.padL = 8.0f;
    row.padR = 4.0f;
    const UiLook theme = parent.state != nullptr ? parent.state->look : UiLook{};
    UiPaint paint = paintToken(theme, UiRole::Panel, UiToken::Title, theme.radiusSm);
    Panel header = parent.child(row, paint);
    if (header.state != nullptr && header.id != kUiNone) {
        header.state->ent[header.id].set<UiTitleBar>({});
    }
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
    const UiLook headerLook = parent.state != nullptr ? parent.state->look : UiLook{};
    UiPaint fold = paintToken(headerLook, UiRole::Button, UiToken::Chrome, headerLook.radiusSm);
    fold.icon = 1;
    Panel foldBtn = header.child(mark, fold);
    if (parent.state != nullptr) {
        uiBind(*parent.state, foldBtn.id, onFold, parent.state);
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
    auto* demo = static_cast<ShellDemo*>(user);
    if (isDir != 0 || demo == nullptr || demo->canvas == nullptr || path == nullptr || !imagePath(path)) {
        return;
    }
    if (canvasLoadImage(*demo->canvas, 0, path)) {
        spdlog::info("browser opened image {}", path);
    }
}

void bindClick(const Panel& box, UiClickFn fn, void* user) {
    if (box.state != nullptr && box.id != kUiNone && fn != nullptr) {
        uiBind(*box.state, box.id, fn, user);
    }
}

void shellBuild(Canvas& canvas, void* user) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (canvas.state == nullptr || demo == nullptr) {
        return;
    }
    filesLoadIcons(canvas);
    ColorBook* colors = demo->colors;
    const int fileSlot = demo->fileDialogSlot;
    *demo = {};
    demo->colors = colors;
    demo->fileDialogSlot = fileSlot;
    demo->canvas = &canvas;
    demo->chipsOn = true;
    demo->toolsOn = true;
    if (colors != nullptr) {
        uiSetColorBook(*canvas.state, colors);
    }
    if (!canvasUseFont(canvas, 12.0f)) {
        spdlog::error("app font was not set");
    }
    if (canvas.state != nullptr) {
        uiBuildShell(*canvas.state);
    }
    filesAttach(*canvas.state);
    filesMount(*canvas.state);
    filesShow(*canvas.state, true);
    demo->scale = uiFindName(*canvas.state, "scale");
    demo->preview = uiFindName(*canvas.state, "preview");
    demo->scaleText = uiFindName(*canvas.state, "scale_text");
    canvasListen(canvas, onScale, demo);
    filesOnOpen(*canvas.state, onBrowse, demo);
    const uint16_t note = uiFindName(*canvas.state, "json_note");
    if (note != kUiNone) {
        uiText(*canvas.state, note, "named in flecs");
    }
    uiMenuStyle(*canvas.state, 210.0f, 30.0f, 8.0f, 4.0f);

    Panel look = Panel::find(*canvas.state, "page_look");
    UiFlex block{};
    block.direction = 0;
    block.gap = 8.0f;
    block.shrink = 0.0f;
    UiPaint clear{};
    clear.role = static_cast<uint8_t>(UiRole::Hidden);
    Panel section = look.child(block, clear);

    const UiLook theme = canvas.state->look;
    Panel heading = section.textButton("YOGA GROW");
    bindClick(heading, toggleChips, demo);

    UiFlex strip{};
    strip.direction = 1;
    strip.heightMode = static_cast<uint8_t>(UiSize::Px);
    strip.height = 44.0f;
    strip.gap = 8.0f;
    strip.shrink = 0.0f;
    strip.drop = 1;
    Panel row = section.child(strip, clear);
    demo->chips = row.id;
    const float grow[3] = {1.0f, 2.0f, 1.0f};
    uint16_t chipId = kUiNone;
    for (int i = 0; i < 3; ++i) {
        UiFlex chipFlex{};
        chipFlex.grow = grow[i];
        chipFlex.shrink = 1.0f;
        chipFlex.heightMode = static_cast<uint8_t>(UiSize::Percent);
        chipFlex.height = 100.0f;
        chipFlex.minW = i == 2 ? 360.0f : 0.0f;
        UiPaint chipPaint = paintToken(theme, UiRole::Panel, UiToken::Accent, theme.radius);
        chipPaint.tag = static_cast<int16_t>(i);
        chipPaint.r = theme.accent[i][0];
        chipPaint.g = theme.accent[i][1];
        chipPaint.b = theme.accent[i][2];
        Panel piece = row.child(chipFlex, chipPaint);
        if (i == 0) {
            chipId = piece.id;
        }
        if (i == 1) {
            piece.animate(theme.accent[1][0], theme.accent[1][1], theme.accent[1][2], 1.0f);
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
    Panel nest = section.child(nestFlex, paintToken(theme, UiRole::Panel, UiToken::Content, theme.radius));
    UiFlex innerFlex{};
    innerFlex.direction = 0;
    innerFlex.pad = 6.0f;
    innerFlex.shrink = 0.0f;
    Panel inner = nest.child(innerFlex, paintToken(theme, UiRole::Panel, UiToken::Item, theme.radius));
    (void)inner.label("NESTED LAYER");

    Panel dropLast = section.textButton("DROP LAST");
    bindClick(dropLast, onDropLast, demo);
    Panel dropFirst = section.textButton("DROP FIRST");
    bindClick(dropFirst, onDropFirst, demo);
    Panel toolsLabel = section.textButton("TOOLS");
    bindClick(toolsLabel, toggleTools, demo);
    UiFlex toolsFlex{};
    toolsFlex.direction = 1;
    toolsFlex.gap = 8.0f;
    toolsFlex.shrink = 0.0f;
    toolsFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    toolsFlex.height = 36.0f;
    Panel tools = section.child(toolsFlex, clear);
    demo->tools = tools.id;
    bindClick(tools.button("SAVE", 4), logPick, demo);
    bindClick(tools.button("PICK", 6), logPick, demo);
    bindClick(tools.button("", 5), onToolWindow, demo);
    bindClick(tools.button("WINDOW", 4), toggleSettings, demo);

    UiFlex homeFlex{};
    homeFlex.direction = 0;
    homeFlex.gap = 6.0f;
    homeFlex.grow = 1.0f;
    homeFlex.shrink = 1.0f;
    homeFlex.minH = 180.0f;
    Panel home = section.child(homeFlex, paintToken(theme, UiRole::Panel, UiToken::Content, theme.radius));
    demo->dockHome = home.id;
    (void)home.label("DOCK");

    UiFlex dockFlex{};
    dockFlex.direction = 0;
    dockFlex.gap = 6.0f;
    dockFlex.pad = 8.0f;
    dockFlex.shrink = 0.0f;
    Panel dock = home.child(dockFlex, paintToken(theme, UiRole::Panel, UiToken::Side, theme.radius));
    demo->dockPanel = dock.id;
    if (canvas.state != nullptr) {
        uiDock(*canvas.state).panel = dock.id;
        uiDock(*canvas.state).home = home.id;
        uiDock(*canvas.state).edges = 15;
    }
    dragHeader(dock, "INSPECTOR");
    (void)dock.label("drag the header out, drop it here or press DOCK");
    bindClick(dock.button("DOCK", 0), onDock, demo);
    bindClick(dock.textButton("LEFT"), onEdgeLeft, demo);
    bindClick(dock.textButton("RIGHT"), onEdgeRight, demo);
    bindClick(dock.textButton("TOP"), onEdgeTop, demo);
    bindClick(dock.textButton("BOTTOM"), onEdgeBottom, demo);

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
    UiPaint settingsPaint = paintToken(theme, UiRole::Panel, UiToken::Window, theme.radius);
    Panel settings = canvas.state != nullptr ? Panel::make(*canvas.state, 0, win, settingsPaint) : Panel{};
    if (canvas.state != nullptr && settings.id != kUiNone) {
        canvas.state->ent[settings.id].set<UiResizable>({});
    }
    demo->settings = settings.id;
    dragHeader(settings, "SETTINGS");
    (void)settings.label("separate window, drag the header");
    bindClick(settings.button("CLOSE", 3), toggleSettings, demo);
    uiShow(*canvas.state, settings.id, false);

    const uint16_t sectionMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    const uint16_t chipMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state) uiSetContextMenu(*canvas.state, section.id, sectionMenu);
    if (canvas.state) uiSetContextMenu(*canvas.state, chipId, chipMenu);
    if (canvas.state == nullptr
        || !uiCommandAddTo(*canvas.state, sectionMenu, "GROW", growCmd, demo)
        || !uiCommandAddTo(*canvas.state, sectionMenu, "PING", ping, nullptr)
        || !uiCommandAddTo(*canvas.state, chipMenu, "WARM", warmCmd, demo)) {
        spdlog::error("context menu was not filled");
    }
    if (!canvasAddCommand(canvas, "PING", ping, nullptr)) {
        spdlog::error("PING command was not added");
    }
}

void toolBuild(Canvas& canvas, void*) {
    if (!canvasUseFont(canvas, 12.0f)) {
        spdlog::error("tool font was not set");
    }
    UiFlex rootFlex{};
    rootFlex.direction = 0;
    rootFlex.pad = 0.0f;
    const UiLook theme = canvas.state != nullptr ? canvas.state->look : UiLook{};
    UiPaint rootPaint = paintToken(theme, UiRole::Panel, UiToken::Window, 0.0f);
    Panel root = canvas.state != nullptr ? Panel::make(*canvas.state, kUiNone, rootFlex, rootPaint) : Panel{};

    UiFlex title{};
    title.direction = 1;
    title.heightMode = static_cast<uint8_t>(UiSize::Px);
    title.height = 40.0f;
    title.align = 1;
    title.shrink = 0.0f;
    title.pad = 10.0f;
    Panel bar = root.child(title, paintToken(theme, UiRole::Panel, UiToken::Title, 0.0f));
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
    UiPaint closePaint = paintToken(theme, UiRole::Close, UiToken::Close, theme.radiusSm);
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
    (void)page.button("PING", 6);
    (void)page.curve();
    (void)page.gradient();
}

void colorBuild(Canvas& canvas, void* user) {
    if (!canvasUseFont(canvas, 12.0f)) {
        spdlog::error("color font was not set");
    }
    if (canvas.state != nullptr) {
        if (user != nullptr) {
            uiSetColorBook(*canvas.state, static_cast<ColorBook*>(user));
        }
        uiBuildColor(*canvas.state);
    }
}

} // namespace burnhope
