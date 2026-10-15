#include "app/Shell.hpp"
#include "files/Browser.hpp"
#include "ui/Domain.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Text.hpp"
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

void toggleChips(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->chips, demo->chipsOn);
    }
}

void toggleTools(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->tools, demo->toolsOn);
    }
}

void toggleSettings(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->settings, demo->settingsOn);
    }
}

void onDock(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiDockTo(*demo->canvas->state, 1);
    }
}

void onToolWindow(void* user, uint16_t) {
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

void onEdgeLeft(void* user, uint16_t) { setEdge(user, 1); }

void onEdgeRight(void* user, uint16_t) { setEdge(user, 2); }

void onEdgeTop(void* user, uint16_t) { setEdge(user, 4); }

void onEdgeBottom(void* user, uint16_t) { setEdge(user, 8); }

void onDropLast(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiSetDrop(*demo->canvas->state, demo->chips, 1);
    }
}

void onDropFirst(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiSetDrop(*demo->canvas->state, demo->chips, 2);
    }
}

void logPick(void* user, uint16_t) {
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
    toggleChips(user, kUiNone);
}

void warmCmd(void* user) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr) {
        canvasSetLook(*demo->canvas, uiLookWarm());
    }
}

void onFold(void* user, uint16_t id) {
    uiFold(*static_cast<UiState*>(user), id);
}

uint16_t dragHeader(UiState& s, uint16_t parent, const char* title) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.heightMode = UiSize::Px;
    row.height = 28.0f;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    row.shrink = 0.0f;
    row.align = UiAlign::Center;
    row.padL = 8.0f;
    row.padR = 4.0f;
    const uint16_t header = uiPanel(s, parent, row, paintToken(s.look, UiToken::Title, s.look.radiusSm));
    if (header != kUiNone) {
        s.ent[header].set<UiTitleBar>({});
    }
    (void)uiLabel(s, header, title);
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.heightMode = UiSize::Px;
    gap.height = 1.0f;
    (void)uiSpacer(s, header, gap);
    UiFlex mark{};
    mark.widthMode = UiSize::Px;
    mark.heightMode = UiSize::Px;
    mark.width = 22.0f;
    mark.height = 22.0f;
    mark.shrink = 0.0f;
    mark.marginR = 2.0f;
    UiPaint fold = paintToken(s.look, UiToken::Chrome, s.look.radiusSm);
    fold.icon = 1;
    const uint16_t foldBtn = uiPanel(s, header, mark, fold);
    uiHover(s, foldBtn);
    uiBind(s, foldBtn, onFold, &s);
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
    if (canvasLoadPhoto(*demo->canvas, 0, path)) {
        spdlog::info("browser opened image {}", path);
    }
}

void bindClick(UiState& s, uint16_t id, UiClickFn fn, void* user) {
    if (id != kUiNone && fn != nullptr) {
        uiBind(s, id, fn, user);
    }
}

uint16_t textButton(UiState& s, uint16_t parent, const char* text) {
    UiButtonConfig cfg{};
    cfg.label = text;
    cfg.height = s.look.glyphH + 6.0f;
    const uint16_t id = uiButton(s, parent, cfg);
    if (id < s.count) {
        if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
        }
    }
    return id;
}

uint16_t iconButton(UiState& s, uint16_t parent, const char* text, uint8_t icon) {
    const bool labeled = text != nullptr && text[0] != '\0';
    UiButtonConfig cfg{};
    cfg.label = labeled ? text : nullptr;
    cfg.height = 30.0f;
    cfg.width = labeled ? 0.0f : 30.0f;
    const uint16_t id = uiButton(s, parent, cfg);
    if (id < s.count) {
        if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
            paint->icon = icon;
        }
    }
    return id;
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

    UiState& s = *canvas.state;
    const uint16_t look = uiFindName(s, "page_look");
    UiFlex block{};
    block.direction = UiFlexDir::Column;
    block.gap = 8.0f;
    block.shrink = 0.0f;
    const uint16_t section = uiPanel(s, look, block, paintClear());

    const UiLook theme = s.look;
    const uint16_t heading = textButton(s, section, "YOGA GROW");
    bindClick(s, heading, toggleChips, demo);

    UiFlex strip{};
    strip.direction = UiFlexDir::Row;
    strip.heightMode = UiSize::Px;
    strip.height = 44.0f;
    strip.gap = 8.0f;
    strip.shrink = 0.0f;
    strip.drop = UiFlexDrop::HideLast;
    const uint16_t row = uiPanel(s, section, strip, paintClear());
    demo->chips = row;
    const float grow[3] = {1.0f, 2.0f, 1.0f};
    uint16_t chipId = kUiNone;
    for (int i = 0; i < 3; ++i) {
        UiFlex chipFlex{};
        chipFlex.grow = grow[i];
        chipFlex.shrink = 1.0f;
        chipFlex.heightMode = UiSize::Percent;
        chipFlex.height = 100.0f;
        chipFlex.minW = i == 2 ? 360.0f : 0.0f;
        UiPaint chipPaint = paintRgb(theme.accent[i][0], theme.accent[i][1], theme.accent[i][2], theme.radius);
        const uint16_t piece = uiPanel(s, row, chipFlex, chipPaint);
        if (i == 0) {
            chipId = piece;
        }
        if (i == 1) {
            uiMotionTo(s, piece, theme.accent[1][0], theme.accent[1][1], theme.accent[1][2], 1.0f);
        }
        if (i == 2) {
            (void)uiLabel(s, piece, "MIN 360");
        }
    }

    UiFlex nestFlex{};
    nestFlex.direction = UiFlexDir::Column;
    nestFlex.pad = 8.0f;
    nestFlex.gap = 4.0f;
    nestFlex.shrink = 0.0f;
    const uint16_t nest = uiPanel(s, section, nestFlex, paintToken(theme, UiToken::Content, theme.radius));
    UiFlex innerFlex{};
    innerFlex.direction = UiFlexDir::Column;
    innerFlex.pad = 6.0f;
    innerFlex.shrink = 0.0f;
    const uint16_t inner = uiPanel(s, nest, innerFlex, paintToken(theme, UiToken::Item, theme.radius));
    (void)uiLabel(s, inner, "NESTED LAYER");

    bindClick(s, textButton(s, section, "DROP LAST"), onDropLast, demo);
    bindClick(s, textButton(s, section, "DROP FIRST"), onDropFirst, demo);
    bindClick(s, textButton(s, section, "TOOLS"), toggleTools, demo);
    UiFlex toolsFlex{};
    toolsFlex.direction = UiFlexDir::Row;
    toolsFlex.gap = 8.0f;
    toolsFlex.shrink = 0.0f;
    toolsFlex.heightMode = UiSize::Px;
    toolsFlex.height = 36.0f;
    const uint16_t tools = uiPanel(s, section, toolsFlex, paintClear());
    demo->tools = tools;
    bindClick(s, iconButton(s, tools, "SAVE", 4), logPick, demo);
    bindClick(s, iconButton(s, tools, "PICK", 6), logPick, demo);
    bindClick(s, iconButton(s, tools, "", 5), onToolWindow, demo);
    bindClick(s, iconButton(s, tools, "WINDOW", 4), toggleSettings, demo);

    UiFlex homeFlex{};
    homeFlex.direction = UiFlexDir::Column;
    homeFlex.gap = 6.0f;
    homeFlex.grow = 1.0f;
    homeFlex.shrink = 1.0f;
    homeFlex.minH = 180.0f;
    const uint16_t home = uiPanel(s, section, homeFlex, paintToken(theme, UiToken::Content, theme.radius));
    demo->dockHome = home;
    (void)uiLabel(s, home, "DOCK");

    UiFlex dockFlex{};
    dockFlex.direction = UiFlexDir::Column;
    dockFlex.gap = 6.0f;
    dockFlex.pad = 8.0f;
    dockFlex.shrink = 0.0f;
    const uint16_t dock = uiPanel(s, home, dockFlex, paintToken(theme, UiToken::Side, theme.radius));
    demo->dockPanel = dock;
    uiDock(s).panel = dock;
    uiDock(s).home = home;
    uiDock(s).edges = 15;
    dragHeader(s, dock, "INSPECTOR");
    (void)uiLabel(s, dock, "drag the header out, drop it here or press DOCK");
    bindClick(s, iconButton(s, dock, "DOCK", 0), onDock, demo);
    bindClick(s, textButton(s, dock, "LEFT"), onEdgeLeft, demo);
    bindClick(s, textButton(s, dock, "RIGHT"), onEdgeRight, demo);
    bindClick(s, textButton(s, dock, "TOP"), onEdgeTop, demo);
    bindClick(s, textButton(s, dock, "BOTTOM"), onEdgeBottom, demo);

    UiFlex win{};
    win.position = UiPosition::Absolute;
    win.posX = 640.0f;
    win.posY = 160.0f;
    win.widthMode = UiSize::Px;
    win.width = 300.0f;
    win.heightMode = UiSize::Px;
    win.height = 168.0f;
    win.pad = 10.0f;
    win.gap = 8.0f;
    win.shrink = 0.0f;
    const uint16_t settings = uiPanel(s, 0, win, paintToken(theme, UiToken::Window, theme.radius));
    if (settings != kUiNone) {
        s.ent[settings].set<UiResizable>({});
    }
    demo->settings = settings;
    dragHeader(s, settings, "SETTINGS");
    (void)uiLabel(s, settings, "separate window, drag the header");
    bindClick(s, iconButton(s, settings, "CLOSE", 3), toggleSettings, demo);
    uiShow(s, settings, false);

    const uint16_t sectionMenu = uiMakeMenu(s);
    const uint16_t chipMenu = uiMakeMenu(s);
    uiSetContextMenu(s, section, sectionMenu);
    uiSetContextMenu(s, chipId, chipMenu);
    if (!uiCommandAddTo(s, sectionMenu, "GROW", growCmd, demo)
        || !uiCommandAddTo(s, sectionMenu, "PING", ping, nullptr)
        || !uiCommandAddTo(s, chipMenu, "WARM", warmCmd, demo)) {
        spdlog::error("context menu was not filled");
    }
    if (!canvasAddCommand(canvas, "PING", ping, nullptr)) {
        spdlog::error("PING command was not added");
    }
}

void toolBuild(Canvas& canvas, void*) {
    if (canvas.state == nullptr || !canvasUseFont(canvas, 12.0f)) {
        spdlog::error("tool font was not set");
        if (canvas.state == nullptr) {
            return;
        }
    }
    UiState& s = *canvas.state;
    UiFlex rootFlex{};
    rootFlex.direction = UiFlexDir::Column;
    const uint16_t root = uiPanel(s, kUiNone, rootFlex, paintToken(s.look, UiToken::Window, 0.0f));
    UiFlex title{};
    title.direction = UiFlexDir::Row;
    title.heightMode = UiSize::Px;
    title.height = 40.0f;
    title.align = UiAlign::Center;
    title.shrink = 0.0f;
    title.pad = 10.0f;
    const uint16_t bar = uiPanel(s, root, title, paintToken(s.look, UiToken::Title, 0.0f));
    (void)uiLabel(s, bar, "SETTINGS");
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    (void)uiSpacer(s, bar, gap);
    UiFlex closeFlex{};
    closeFlex.widthMode = UiSize::Px;
    closeFlex.heightMode = UiSize::Px;
    closeFlex.width = 28.0f;
    closeFlex.height = 28.0f;
    closeFlex.shrink = 0.0f;
    UiPaint closePaint = paintToken(s.look, UiToken::Close, s.look.radiusSm);
    closePaint.icon = 3;
    const uint16_t closeB = uiPanel(s, bar, closeFlex, closePaint);
    uiHover(s, closeB);
    UiFlex body{};
    body.direction = UiFlexDir::Column;
    body.pad = 16.0f;
    body.gap = 10.0f;
    body.grow = 1.0f;
    const uint16_t page = uiPanel(s, root, body, paintClear());
    (void)uiLabel(s, page, "Separate OS window.");
    (void)uiLabel(s, page, "Drag the title. Resize any edge.");
    (void)iconButton(s, page, "PING", 6);
    (void)uiCurve(s, page);
    (void)uiGradient(s, page);
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
