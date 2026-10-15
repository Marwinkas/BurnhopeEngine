#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "files/Format.hpp"
#include "files/Model.hpp"
#include "platform/Desktop.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/VirtualGrid.hpp"
#include "ui/widget/Image.hpp"

#include "ui/Domain.hpp"
#include "ui/widget/Detail.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {
namespace {
namespace fs = std::filesystem;

struct Browser : FileModel {
    FileLook look{};
    FileOpenFn onOpen = nullptr;
    void* user = nullptr;
    uint16_t plus = kUiNone;
    uint16_t back = kUiNone;
    uint16_t fwd = kUiNone;
    uint16_t viewBtn = kUiNone;
    uint16_t searchBtn = kUiNone;
    uint16_t searchBox = kUiNone;
    uint16_t filterBtn = kUiNone;
    uint16_t pop = kUiNone;
    uint16_t optIcon = kUiNone;
    uint16_t optList = kUiNone;
    uint16_t sortName = kUiNone;
    uint16_t sortSize = kUiNone;
    uint16_t sortType = kUiNone;
    uint16_t sortDate = kUiNone;
    uint16_t optGroup = kUiNone;
    uint16_t crumbs = kUiNone;
    uint16_t wTree = kUiNone;
    uint16_t status = kUiNone;
    uint16_t wRename = kUiNone;
    uint8_t ctrl = 0;
    uint8_t shift = 0;
    uint8_t searchOn = 0;
    uint8_t popOn = 0;
    uint8_t listView = 0;
    uint8_t dragOn = 0;
    uint8_t dragArm = 0;
    uint8_t bandSnap = 0;
    uint8_t bandKeep[kFileCap]{};
    float dragX = 0.0f;
    float dragY = 0.0f;
    uint16_t ghost = kUiNone;
    uint8_t folderSlot = 255;
    uint8_t fileSlot = 255;
    uint8_t mounted = 0;
};

uint8_t gFolder = 255;
uint8_t gFile = 255;

Browser& book(UiState& s) {
    if (uiFiles(s).files == nullptr) {
        auto* created = new Browser();
        created->folderSlot = gFolder;
        created->fileSlot = gFile;
        uiFiles(s).files = created;
        created->load();
        std::error_code ec;
        if (!fs::is_directory(fs::path(created->cwd), ec)) {
            copyCap(created->cwd, 512, ".");
        }
        copyCap(created->hist[0], 512, created->cwd);
        created->histN = 1;
        created->reload();
    }
    return *static_cast<Browser*>(uiFiles(s).files);
}

void refresh(UiState& s);

void bindFileIcon(UiState& s, uint16_t iconId, uint8_t photoSlot, uint8_t procIcon) {
    if (iconId >= s.count) {
        return;
    }
    auto* paint = s.ent[iconId].try_get_mut<UiPaint>();
    if (photoSlot < kPhotoSlots) {
        UiImage image{};
        if (const UiImage* live = s.ent[iconId].try_get<UiImage>()) {
            image = *live;
        }
        image.slot = photoSlot;
        image.frames = image.frames == 0 ? 1 : image.frames;
        image.viewW = 1.0f;
        image.viewH = 1.0f;
        s.ent[iconId].set<UiImage>(image);
        if (paint != nullptr) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
            paint->icon = 0;
        }
        return;
    }
    if (s.ent[iconId].has<UiImage>()) {
        s.ent[iconId].remove<UiImage>();
    }
    if (paint != nullptr) {
        paint->a = 0.0f;
        paint->borderW = 0.0f;
        paint->shadow = 0.0f;
        paint->icon = procIcon;
        paint->r = s.look.text[0];
        paint->g = s.look.text[1];
        paint->b = s.look.text[2];
    }
}

void openAt(UiState& s, int index) {
    Browser& b = book(s);
    if (index < 0 || index >= b.count) {
        return;
    }
    char path[512]{};
    joinPath(path, 512, b.cwd, b.items[index].name);
    if (b.items[index].isDir != 0) {
        if (uiFiles(s).fileView != kUiNone) {
            uiVirtualGridClearClick(s, uiFiles(s).fileView);
        }
        b.go(path);
        if (b.onOpen != nullptr) {
            b.onOpen(b.user, b.cwd, 1);
        }
    } else if (b.onOpen != nullptr) {
        if (!desktopOpen(path)) {
            b.setStatus("cannot open");
        }
        b.onOpen(b.user, path, 0);
    }
    refresh(s);
}

void makeFolder(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    char dst[512]{};
    uniquePath(dst, 512, b.cwd, "New Folder", "");
    std::error_code ec;
    fs::create_directory(fs::path(dst), ec);
    if (ec) {
        b.setStatus("create failed");
        return;
    }
    b.reload();
    const char* name = std::strrchr(dst, '/');
    name = name != nullptr ? name + 1 : dst;
    for (int i = 0; i < b.count; ++i) {
        if (sameText(b.items[i].name, name)) {
            b.selectOnly(i);
            b.startRename(i);
            break;
        }
    }
    b.setStatus("new folder");
    refresh(s);
}

void makeFile(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).touchFile("New Document.txt");
    book(s).reload();
    refresh(s);
}

void onPlus(void* user, uint16_t id) {
    UiState& s = *static_cast<UiState*>(user);
    const UiBox& box = s.box[id];
    const UiMenuItem items[] = {
        {.label = "New Folder", .onClick = makeFolder},
        {.label = "New Text Document", .onClick = makeFile},
    };
    uiMenuOpen(s, box.x, box.y + box.h, items, user, box.y);
}

void onBack(void* user, uint16_t) {
    UiState& s = *static_cast<UiState*>(user);
    if (uiFiles(s).fileView != kUiNone) {
        uiVirtualGridClearClick(s, uiFiles(s).fileView);
    }
    book(s).goBack();
    refresh(s);
}

void onFwd(void* user, uint16_t) {
    UiState& s = *static_cast<UiState*>(user);
    if (uiFiles(s).fileView != kUiNone) {
        uiVirtualGridClearClick(s, uiFiles(s).fileView);
    }
    book(s).goForward();
    refresh(s);
}

void onNav(void* user, const char* path) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).go(path);
    if (uiFiles(s).fileView != kUiNone) {
        uiVirtualGridClearClick(s, uiFiles(s).fileView);
    }
    refresh(s);
}

void onTree(void* user, uint32_t index) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    if (index >= static_cast<uint32_t>(b.treeN)) {
        return;
    }
    char path[512]{};
    if (b.FileModel::tree[index][0] == '\0') {
        copyCap(path, 512, b.root[0] != '\0' ? b.root : b.cwd);
    } else {
        joinPath(path, 512, b.root[0] != '\0' ? b.root : b.cwd, b.FileModel::tree[index]);
    }
    b.go(path);
    refresh(s);
}

void bindNode(void* user, uint16_t row, uint32_t index) {
    auto* s = static_cast<UiState*>(user);
    Browser& b = book(*s);
    if (index >= static_cast<uint32_t>(b.treeN)) {
        return;
    }
    uiRowIndent(*s, row, static_cast<float>(b.treeDepth[index]) * 16.0f);
    const UiParts* parts = s->ent[row].try_get<UiParts>();
    if (parts == nullptr) {
        return;
    }
    uiShow(*s, parts->chevron, true);
    uiText(*s, parts->chevron, "");
    if (parts->chevron < s->count) {
        if (auto* paint = s->ent[parts->chevron].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
            paint->disabled = 0;
            paint->icon = b.treeOpen[index] != 0 ? 11 : 10;
            paint->r = s->look.text[0];
            paint->g = s->look.text[1];
            paint->b = s->look.text[2];
        }
    }
    bindFileIcon(*s, parts->icon, b.folderSlot, 4);
    const char* shown = b.FileModel::tree[index];
    if (shown[0] == '\0') {
        const char* base = b.root[0] != '\0' ? b.root : b.cwd;
        const char* leaf = std::strrchr(base, '/');
        shown = leaf != nullptr && leaf[1] != '\0' ? leaf + 1 : base;
    } else {
        const char* slash = std::strrchr(shown, '/');
        if (slash != nullptr && slash[1] != '\0') {
            shown = slash + 1;
        }
    }
    uiText(*s, parts->label, shown);
}

void onToggle(void* user, uint32_t index) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    if (index < static_cast<uint32_t>(b.treeN)) {
        b.treeOpen[index] = b.treeOpen[index] == 0 ? 1 : 0;
        b.rebuildTree();
        refresh(s);
    }
}

void bindCell(void* user, uint16_t cell, uint32_t index) {
    auto* s = static_cast<UiState*>(user);
    Browser& b = book(*s);
    if (index >= static_cast<uint32_t>(b.count)) {
        return;
    }
    const bool header = b.items[index].isDir == 2;
    const bool editing = !header && b.renameAt == static_cast<int>(index);
    const bool on = !header && b.items[index].selected != 0;
    const UiParts* parts = s->ent[cell].try_get<UiParts>();
    const uint16_t label = parts != nullptr && parts->label != kUiNone ? parts->label : cell;
    uiText(*s, label, editing ? "" : b.items[index].name);
    if (auto* paint = s->ent[cell].try_get_mut<UiPaint>()) {
        if (on) {
            *paint = paintToken(s->look, UiToken::Accent, s->look.radiusSm);
            paint->a = 0.92f;
            paint->borderW = 0.0f;
        } else if (header) {
            *paint = paintToken(s->look, UiToken::Side, 0.0f);
            paint->a = 0.95f;
            paint->borderW = 0.0f;
        } else {
            *paint = paintToken(s->look, UiToken::Item, s->look.radiusSm);
            paint->a = 0.0f;
            paint->borderW = 0.0f;
        }
    }
    if (parts != nullptr && parts->icon < s->count) {
        uiShow(*s, parts->icon, !header);
        const uint8_t photo = b.items[index].isDir != 0 ? b.folderSlot : b.fileSlot;
        const uint8_t proc = b.items[index].isDir != 0 ? 4 : 2;
        bindFileIcon(*s, parts->icon, header ? 255 : photo, proc);
    }
}

void onSelect(void* user, uint32_t index, bool) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    const int at = static_cast<int>(index);
    if (at < 0 || at >= b.count) {
        return;
    }
    if (b.items[at].isDir == 2) {
        const uint8_t bit = static_cast<uint8_t>(1u << (b.items[at].kind & 7));
        b.groupOpen = static_cast<uint8_t>(b.groupOpen ^ bit);
        b.reload();
        refresh(s);
        return;
    }
    if (b.shift != 0) {
        b.selectRange(b.anchor, at);
    } else if (b.ctrl != 0) {
        b.toggleSelect(at);
    } else {
        b.selectOnly(at);
    }
    refresh(s);
}

void onOpen(void* user, uint32_t index) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    if (b.ctrl != 0 || b.shift != 0) {
        return;
    }
    if (index < static_cast<uint32_t>(b.count) && b.items[index].isDir == 2) {
        return;
    }
    openAt(s, static_cast<int>(index));
}

void onBandBegin(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    if (b.bandSnap == 0) {
        for (int i = 0; i < b.count && i < kFileCap; ++i) {
            b.bandKeep[i] = b.items[i].selected;
        }
        b.bandSnap = 1;
    }
    for (int i = 0; i < b.count; ++i) {
        b.items[i].selected = b.ctrl != 0 && i < kFileCap ? b.bandKeep[i] : 0;
    }
    if (uiFiles(s).fileView != kUiNone) {
        uiVirtualGridClearClick(s, uiFiles(s).fileView);
    }
}

void onBand(void* user, uint16_t child) {
    UiState& s = *static_cast<UiState*>(user);
    const UiVirtRow* row = child < s.count ? s.ent[child].try_get<UiVirtRow>() : nullptr;
    Browser& b = book(s);
    if (row != nullptr && row->index < static_cast<uint32_t>(b.count) && b.items[row->index].isDir != 2) {
        b.items[row->index].selected = 1;
        refresh(s);
    }
}

void onSearch(void* user, const char* text) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    if (!sameText(b.search, text)) {
        copyCap(b.search, 40, text);
        b.reload();
        refresh(s);
    }
}

void applyKind(UiState& s, int kind) {
    Browser& b = book(s);
    b.kind = kind;
    b.reload();
    refresh(s);
}

void filtAll(void* user) { applyKind(*static_cast<UiState*>(user), 0); }
void filtDir(void* user) { applyKind(*static_cast<UiState*>(user), 1); }
void filtImage(void* user) { applyKind(*static_cast<UiState*>(user), 2); }
void filtText(void* user) { applyKind(*static_cast<UiState*>(user), 3); }
void filtAudio(void* user) { applyKind(*static_cast<UiState*>(user), 4); }

void onFilter(void* user, uint16_t id) {
    UiState& s = *static_cast<UiState*>(user);
    const UiBox& box = s.box[id];
    const UiMenuItem items[] = {
        {.label = "All", .onClick = filtAll},
        {.label = "Folders", .onClick = filtDir},
        {.label = "Images", .onClick = filtImage},
        {.label = "Text", .onClick = filtText},
        {.label = "Audio", .onClick = filtAudio},
    };
    uiMenuOpen(s, box.x, box.y + box.h, items, user, box.y);
}

void onSearchBtn(void* user, uint16_t) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    b.searchOn = b.searchOn == 0 ? 1 : 0;
    const bool on = b.searchOn != 0;
    if (b.searchBox != kUiNone) {
        uiShow(s, b.searchBox, on);
        if (on) {
            uiFocus(s, b.searchBox);
        }
    }
    if (b.filterBtn != kUiNone) {
        uiShow(s, b.filterBtn, on);
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

uint16_t square(UiState& s, uint16_t parent, const char* label, UiClickFn fn) {
    UiButtonConfig cfg{};
    cfg.label = label;
    cfg.width = 28.0f;
    cfg.height = 28.0f;
    cfg.onClick = fn;
    cfg.user = &s;
    return uiButton(s, parent, cfg);
}

uint16_t iconSquare(UiState& s, uint16_t parent, uint8_t icon, UiClickFn fn) {
    const uint16_t id = square(s, parent, nullptr, fn);
    if (id < s.count) {
        if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
            paint->icon = icon;
        }
    }
    return id;
}

void refresh(UiState& s) {
    Browser& b = book(s);
    b.rebuildTree();
    if (b.crumbs != kUiNone) {
        uiBreadcrumbsSet(s, b.crumbs, b.cwd, b.root[0] != '\0' ? b.root : nullptr);
    }
    if (b.wTree != kUiNone && b.wTree < s.count && s.ent[b.wTree].is_alive()) {
        if (auto* tree = s.ent[b.wTree].try_get_mut<UiTreeView>()) {
            tree->count = static_cast<uint32_t>(b.treeN);
            uiTreeViewRefresh(s, b.wTree);
        }
    }
    if (uiFiles(s).fileView != kUiNone && uiFiles(s).fileView < s.count && s.ent[uiFiles(s).fileView].is_alive()) {
        if (auto* grid = s.ent[uiFiles(s).fileView].try_get_mut<UiVirtualGrid>()) {
            const bool rows = b.listView != 0 || b.groupOn != 0;
            grid->count = static_cast<uint32_t>(b.count);
            grid->list = rows ? 1 : 0;
            grid->itemW = rows ? 480.0f : b.look.icon;
            grid->itemH = rows ? 36.0f : b.look.icon;
            uiVirtualGridRefresh(s, uiFiles(s).fileView);
        }
    }
    auto disable = [&](uint16_t id, bool off) {
        if (id < s.count) {
            if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
                paint->disabled = off ? 1 : 0;
            }
        }
    };
    disable(b.back, !b.canBack());
    disable(b.fwd, !b.canForward());
    if (b.wRename != kUiNone) {
        uiShow(s, b.wRename, b.renameAt >= 0);
    }
    if (b.status != kUiNone) {
        char foot[96]{};
        b.formatFoot(foot, 96);
        uiText(s, b.status, foot);
    }
    s.visualDirty = true;
}

UiFlex rowH(float h) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.heightMode = UiSize::Px;
    row.height = h;
    row.shrink = 0.0f;
    row.align = UiAlign::Center;
    row.gap = 6.0f;
    row.padL = 4.0f;
    row.padR = 4.0f;
    return row;
}

void cmdIcons(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).listView = 0;
    refresh(s);
}

void cmdList(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).listView = 1;
    refresh(s);
}

void cmdSortAt(UiState& s, int key) {
    Browser& b = book(s);
    b.sortKey = key;
    b.reload();
    refresh(s);
}

void cmdSortName(void* user) { cmdSortAt(*static_cast<UiState*>(user), 0); }
void cmdSortSize(void* user) { cmdSortAt(*static_cast<UiState*>(user), 1); }
void cmdSortType(void* user) { cmdSortAt(*static_cast<UiState*>(user), 2); }
void cmdSortDate(void* user) { cmdSortAt(*static_cast<UiState*>(user), 3); }

void cmdGroup(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    b.groupOn = b.groupOn == 0 ? 1 : 0;
    b.reload();
    refresh(s);
}

void mountFiles(UiState& s) {
    if (uiFiles(s).fileFrame == kUiNone || book(s).mounted != 0) {
        return;
    }
    Browser& b = book(s);
    b.mounted = 1;
    const uint16_t bar = uiPanel(s, uiFiles(s).fileFrame, rowH(32.0f), paintClear());
    b.plus = square(s, bar, "+", onPlus);
    b.back = square(s, bar, "<", onBack);
    b.fwd = square(s, bar, ">", onFwd);
    b.viewBtn = iconSquare(s, bar, 6, nullptr);
    const uint16_t viewMenu = uiMakeMenu(s);
    if (b.viewBtn != kUiNone && viewMenu != kUiNone) {
        uiSetContextMenu(s, b.viewBtn, viewMenu);
        uiMarkMenuBar(s, b.viewBtn);
        (void)uiCommandAddTo(s, viewMenu, "Icons", cmdIcons, &s);
        (void)uiCommandAddTo(s, viewMenu, "List", cmdList, &s);
        (void)uiCommandAddTo(s, viewMenu, "Sort by name", cmdSortName, &s);
        (void)uiCommandAddTo(s, viewMenu, "Sort by size", cmdSortSize, &s);
        (void)uiCommandAddTo(s, viewMenu, "Sort by type", cmdSortType, &s);
        (void)uiCommandAddTo(s, viewMenu, "Sort by date", cmdSortDate, &s);
        (void)uiCommandAddTo(s, viewMenu, "Group by type", cmdGroup, &s);
    }
    UiFlex spacer{};
    spacer.grow = 1.0f;
    spacer.shrink = 1.0f;
    spacer.heightMode = UiSize::Px;
    spacer.height = 28.0f;
    (void)uiPanel(s, bar, spacer, paintClear());
    UiFlex body{};
    body.direction = UiFlexDir::Row;
    body.grow = 1.0f;
    body.shrink = 1.0f;
    body.minH = 80.0f;
    const uint16_t mid = uiPanel(s, uiFiles(s).fileFrame, body, paintClear());
    UiFlex side{};
    side.widthMode = UiSize::Px;
    side.width = b.look.tree;
    side.heightMode = UiSize::Percent;
    side.height = 100.0f;
    side.shrink = 0.0f;
    const uint16_t pane = uiPanel(s, mid, side, paintToken(s.look, UiToken::Side, 0.0f));
    UiTreeConfig tree{};
    tree.totalNodes = static_cast<uint32_t>(b.treeN);
    tree.onBindNode = bindNode;
    tree.onToggleNode = onToggle;
    tree.onSelectNode = onTree;
    tree.user = &s;
    b.wTree = uiTreeView(s, pane, tree);
    (void)uiSplitBar(s, mid, true);
    UiFlex gridHost{};
    gridHost.grow = 1.0f;
    gridHost.shrink = 1.0f;
    gridHost.direction = UiFlexDir::Column;
    const uint16_t right = uiPanel(s, mid, gridHost, paintClear());
    UiFlex crumbRow = rowH(30.0f);
    crumbRow.shrink = 0.0f;
    const uint16_t crumbBar = uiPanel(s, right, crumbRow, paintClear());
    b.crumbs = uiBreadcrumbs(s, crumbBar, onNav, &s);
    UiFlex crumbSpacer{};
    crumbSpacer.grow = 1.0f;
    crumbSpacer.shrink = 1.0f;
    crumbSpacer.heightMode = UiSize::Px;
    crumbSpacer.height = 28.0f;
    (void)uiPanel(s, crumbBar, crumbSpacer, paintClear());
    b.searchBox = uiSearchField(s, crumbBar, "search", onSearch, &s, 180.0f);
    UiButtonConfig filterCfg{};
    filterCfg.label = "Filter";
    filterCfg.width = 72.0f;
    filterCfg.height = 28.0f;
    filterCfg.onClick = onFilter;
    filterCfg.user = &s;
    b.filterBtn = uiButton(s, crumbBar, filterCfg);
    b.searchBtn = iconSquare(s, crumbBar, 7, onSearchBtn);
    if (b.searchBox != kUiNone) {
        uiShow(s, b.searchBox, false);
    }
    if (b.filterBtn != kUiNone) {
        uiShow(s, b.filterBtn, false);
    }
    UiFlex gridPane{};
    gridPane.grow = 1.0f;
    gridPane.shrink = 1.0f;
    gridPane.minH = 64.0f;
    const uint16_t gridPaneId = uiPanel(s, right, gridPane, paintClear());
    UiVirtualGridConfig cfg{};
    cfg.itemW = b.look.icon;
    cfg.itemH = b.look.icon;
    cfg.totalItems = static_cast<uint32_t>(b.count);
    cfg.onBindCell = bindCell;
    cfg.onSelect = onSelect;
    cfg.onActivate = onOpen;
    cfg.user = &s;
    uiFiles(s).fileView = uiVirtualGrid(s, gridPaneId, cfg);
    (void)uiRubberband(s, uiFiles(s).fileView, onBand, &s, onBandBegin);
    const uint16_t foot = uiPanel(s, uiFiles(s).fileFrame, rowH(24.0f), paintClear());
    UiFlex label{};
    label.grow = 1.0f;
    label.shrink = 1.0f;
    label.heightMode = UiSize::Px;
    label.height = 18.0f;
    b.status = uiPanel(s, foot, label, paintLabel(s.look));
    uiText(s, b.status, "");
    UiFlex ghostFlex = px(36.0f, 22.0f);
    ghostFlex.position = UiPosition::Absolute;
    UiPaint ghostPaint = paintToken(s.look, UiToken::Menu, s.look.radiusSm);
    ghostPaint.a = 0.86f;
    b.ghost = uiPanel(s, 0, ghostFlex, ghostPaint);
    if (b.ghost != kUiNone) {
        s.ent[b.ghost].set<UiPopup>({});
        uiText(s, b.ghost, "0");
        uiShow(s, b.ghost, false);
    }
    UiFlex rename{};
    rename.position = UiPosition::Absolute;
    rename.widthMode = UiSize::Px;
    rename.heightMode = UiSize::Px;
    rename.width = 160.0f;
    rename.height = 24.0f;
    rename.shrink = 0.0f;
    b.wRename = uiPanel(s, 0, rename, paintToken(s.look, UiToken::Field, 4.0f));
    if (b.wRename != kUiNone) {
        s.ent[b.wRename].set<UiField>({});
        s.ent[b.wRename].add<UiFocusable>();
        s.ent[b.wRename].add<UiTextInput>();
        s.ent[b.wRename].set<UiPopup>({});
        uiShow(s, b.wRename, false);
    }
    refresh(s);
}

void openFileMenu(void* user, float x, float y, bool onItem) {
    UiState& s = *static_cast<UiState*>(user);
    if (onItem) {
        const UiMenuItem items[] = {
            {.label = "Open", .onClick = [](void* u) {
                 UiState& st = *static_cast<UiState*>(u);
                 Browser& b = book(st);
                 for (int i = 0; i < b.count; ++i) {
                     if (b.items[i].selected != 0) {
                         openAt(st, i);
                         return;
                     }
                 }
             }},
            {.label = "Rename (F2)", .onClick = [](void* u) {
                 UiState& st = *static_cast<UiState*>(u);
                 Browser& b = book(st);
                 for (int i = 0; i < b.count; ++i) {
                     if (b.items[i].selected != 0) {
                         b.startRename(i);
                         refresh(st);
                         return;
                     }
                 }
             }},
            {.label = "Duplicate (Ctrl+D)", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).duplicate(); refresh(*static_cast<UiState*>(u)); }},
            {.label = "", .separator = true},
            {.label = "Copy (Ctrl+C)", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).copySelection(0); }},
            {.label = "Cut (Ctrl+X)", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).copySelection(1); }},
            {.label = "", .separator = true},
            {.label = "Delete (Del)", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).removeSelected(); refresh(*static_cast<UiState*>(u)); }},
            {.label = "Reveal in OS", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).reveal(); }},
        };
        uiMenuOpen(s, x, y, items, user);
        return;
    }
    const UiMenuItem items[] = {
        {.label = "New Folder", .onClick = makeFolder},
        {.label = "New File", .onClick = makeFile},
        {.label = "Paste (Ctrl+V)", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).paste(); refresh(*static_cast<UiState*>(u)); }},
        {.label = "Select All (Ctrl+A)", .onClick = [](void* u) { book(*static_cast<UiState*>(u)).selectAll(); refresh(*static_cast<UiState*>(u)); }},
    };
    uiMenuOpen(s, x, y, items, user);
}

bool dropPath(UiState& s, char* dst, int cap) {
    Browser& b = book(s);
    for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == b.wTree) {
            break;
        }
        const UiVirtRow* row = s.ent[p].try_get<UiVirtRow>();
        if (row == nullptr) {
            continue;
        }
        if (uiWithin(s, p, b.wTree) && row->index < static_cast<uint32_t>(b.treeN)) {
            if (b.FileModel::tree[row->index][0] == '\0') {
                copyCap(dst, cap, b.root[0] != '\0' ? b.root : b.cwd);
            } else {
                joinPath(dst, cap, b.root[0] != '\0' ? b.root : b.cwd, b.FileModel::tree[row->index]);
            }
            return true;
        }
        if (uiFiles(s).fileView != kUiNone && uiWithin(s, p, uiFiles(s).fileView) && row->index < static_cast<uint32_t>(b.count)
            && b.items[row->index].isDir == 1) {
            joinPath(dst, cap, b.cwd, b.items[row->index].name);
            return true;
        }
    }
    if (b.crumbs != kUiNone && (s.hovered == b.crumbs || uiWithin(s, s.hovered, b.crumbs))) {
        copyCap(dst, cap, b.cwd);
        return dst[0] != '\0';
    }
    return false;
}

void hoverFoot(UiState& s) {
    Browser& b = book(s);
    if (b.status == kUiNone) {
        return;
    }
    char line[96]{};
    for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        const UiVirtRow* row = s.ent[p].try_get<UiVirtRow>();
        if (row == nullptr || uiFiles(s).fileView == kUiNone || !uiWithin(s, p, uiFiles(s).fileView)) {
            continue;
        }
        if (row->index < static_cast<uint32_t>(b.count) && b.items[row->index].isDir != 2) {
            const FileItem& item = b.items[row->index];
            char size[32]{};
            formatFileSize(item.bytes, size, 32);
            std::snprintf(line, sizeof(line), "%s  %s", item.name, size);
            uiText(s, b.status, line);
            return;
        }
    }
    b.formatFoot(line, 96);
    if (line[0] == '\0') {
        std::snprintf(line, sizeof(line), "%d", b.count);
    }
    uiText(s, b.status, line);
}

bool placeRename(UiState& s) {
    Browser& b = book(s);
    if (b.wRename >= s.count || b.renameAt < 0) {
        return false;
    }
    const uint16_t view = uiFiles(s).fileView;
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiVirtRow* row = s.ent[id].try_get<UiVirtRow>();
        if (row == nullptr || row->index != static_cast<uint32_t>(b.renameAt) || !uiWithin(s, id, view)) {
            continue;
        }
        const UiParts* parts = s.ent[id].try_get<UiParts>();
        const uint16_t lab = parts != nullptr && parts->label < s.count ? parts->label : id;
        const UiBox& box = s.box[lab];
        auto* flex = s.ent[b.wRename].try_get_mut<UiFlex>();
        if (flex == nullptr || box.w < 8.0f || box.h < 8.0f) {
            continue;
        }
        const float w = box.w > 48.0f ? box.w : 160.0f;
        const float h = box.h > 16.0f ? box.h : 22.0f;
        const bool moved = flex->posX != box.x || flex->posY != box.y || flex->width != w || flex->height != h;
        flex->position = UiPosition::Absolute;
        flex->posX = box.x;
        flex->posY = box.y;
        flex->widthMode = UiSize::Px;
        flex->heightMode = UiSize::Px;
        flex->width = w;
        flex->height = h;
        if (moved) {
            s.layoutDirty = true;
        }
        return true;
    }
    return false;
}

bool filesKeys(UiState& s, const InputFrame& in) {
    if (uiFiles(s).files == nullptr || uiFiles(s).fileFrame == kUiNone) {
        return false;
    }
    Browser& b = book(s);
    b.ctrl = in.ctrl;
    b.shift = in.shift;
    if (in.ctrl != 0 && in.wheel != 0.0f && uiFiles(s).fileView != kUiNone && uiFiles(s).fileView < s.count) {
        const uint16_t view = uiFiles(s).fileView;
        if (s.hovered == view || uiWithin(s, s.hovered, view)) {
            b.look.icon = std::clamp(b.look.icon + in.wheel * 8.0f, 48.0f, 256.0f);
            refresh(s);
            return true;
        }
    }
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    if (press) {
        b.dragX = in.pointer.x;
        b.dragY = in.pointer.y;
        b.dragOn = 0;
        b.dragArm = 0;
        for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            const UiVirtRow* row = s.ent[p].try_get<UiVirtRow>();
            if (row != nullptr && uiFiles(s).fileView != kUiNone && uiWithin(s, p, uiFiles(s).fileView)
                && row->index < static_cast<uint32_t>(b.count) && b.items[row->index].selected != 0) {
                b.dragArm = 1;
            }
        }
    }
    if (down && b.dragArm != 0 && b.dragOn == 0) {
        const float dx = in.pointer.x - b.dragX;
        const float dy = in.pointer.y - b.dragY;
        if (dx * dx + dy * dy > 36.0f) {
            b.dragOn = 1;
        }
    }
    if (b.ghost != kUiNone) {
        uiShow(s, b.ghost, b.dragOn != 0);
        if (b.dragOn != 0) {
            char badge[32]{};
            std::snprintf(badge, sizeof(badge), "%d", b.selectedCount());
            uiText(s, b.ghost, badge);
            uiPlace(s, b.ghost, true, in.pointer.x + 12.0f, in.pointer.y + 12.0f, 36.0f, 22.0f);
        }
    }
    if (release) {
        b.bandSnap = 0;
    }
    if (press && b.dragOn == 0) {
        bool onItem = false;
        for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].try_get<UiVirtRow>() != nullptr) {
                onItem = true;
                break;
            }
        }
        const bool inFiles = (uiFiles(s).fileView != kUiNone && (s.hovered == uiFiles(s).fileView || uiWithin(s, s.hovered, uiFiles(s).fileView)))
            || (b.wTree != kUiNone && (s.hovered == b.wTree || uiWithin(s, s.hovered, b.wTree)));
        if (inFiles && !onItem) {
            b.clearSelection();
            refresh(s);
        }
    }
    if (b.dragOn != 0) {
        for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            const UiVirtRow* row = s.ent[p].try_get<UiVirtRow>();
            if (row == nullptr) {
                continue;
            }
            const bool folder = (uiWithin(s, p, b.wTree) && row->index < static_cast<uint32_t>(b.treeN))
                || (uiFiles(s).fileView != kUiNone && uiWithin(s, p, uiFiles(s).fileView) && row->index < static_cast<uint32_t>(b.count)
                    && b.items[row->index].isDir == 1);
            if (!folder) {
                continue;
            }
            if (auto* paint = s.ent[p].try_get_mut<UiPaint>()) {
                paint->borderW = 1.0f;
                paint->br = s.look.accent[0][0];
                paint->bg = s.look.accent[0][1];
                paint->bb = s.look.accent[0][2];
            }
            break;
        }
    }
    if (release && b.dragOn != 0) {
        char dest[512]{};
        if (dropPath(s, dest, 512)) {
            b.moveInto(dest);
        }
        b.dragOn = 0;
        b.dragArm = 0;
        if (b.ghost != kUiNone) {
            uiShow(s, b.ghost, false);
        }
        refresh(s);
        return true;
    }
    hoverFoot(s);
    if (press && b.popOn != 0) {
        const bool inside = s.hovered == b.viewBtn || s.hovered == b.pop || uiWithin(s, s.hovered, b.pop);
        if (!inside && b.pop != kUiNone) {
            uiShow(s, b.pop, false);
            b.popOn = 0;
        }
    }
    if (b.searchOn != 0 && s.focused == b.searchBox) {
        return false;
    }
    if (b.renameAt >= 0 && b.wRename != kUiNone && placeRename(s)) {
        const bool shown = nodeShown(s, b.wRename);
        uiShow(s, b.wRename, true);
        if (!shown) {
            uiFocus(s, b.wRename);
        }
    }
    const bool field = s.focused == b.wRename;
    const bool right = (in.pointer.pressed & kPointerRight) != 0;
    if (field && in.enter != 0) {
        const UiField* text = s.ent[b.wRename].try_get<UiField>();
        b.commitRename(text != nullptr ? text->bytes : "");
        uiShow(s, b.wRename, false);
        refresh(s);
        return true;
    }
    if (field && in.escape != 0) {
        b.cancelRename();
        uiShow(s, b.wRename, false);
        refresh(s);
        return true;
    }
    if (field) {
        return false;
    }
    if (in.f2 != 0) {
        for (int i = 0; i < b.count; ++i) {
            if (b.items[i].selected != 0) {
                b.startRename(i);
                if (b.wRename != kUiNone) {
                    uiSetFieldText(s, b.wRename, b.items[i].name);
                    if (auto* field = s.ent[b.wRename].try_get_mut<UiField>()) {
                        field->anchor = 0;
                    }
                    if (placeRename(s)) {
                        uiShow(s, b.wRename, true);
                        uiFocus(s, b.wRename);
                    } else {
                        uiShow(s, b.wRename, false);
                    }
                }
                refresh(s);
                return true;
            }
        }
    }
    if (in.del != 0) {
        b.removeSelected();
        refresh(s);
        return true;
    }
    if (in.copy != 0) {
        b.copySelection(0);
        return true;
    }
    if (in.cut != 0) {
        b.copySelection(1);
        return true;
    }
    if (in.pasteOn != 0) {
        b.paste();
        refresh(s);
        return true;
    }
    if (in.selectAll != 0) {
        b.selectAll();
        refresh(s);
        return true;
    }
    if (in.ctrl != 0 && in.textLen == 1 && (in.text[0] == 'd' || in.text[0] == 'D')) {
        b.duplicate();
        refresh(s);
        return true;
    }
    if (in.enter != 0) {
        for (int i = 0; i < b.count; ++i) {
            if (b.items[i].selected != 0) {
                openAt(s, i);
                return true;
            }
        }
    }
    if (right) {
        const uint16_t frame = uiFiles(s).fileFrame;
        if (frame >= s.count || !hitBox(s.box[frame], in.pointer.x, in.pointer.y)) {
            return false;
        }
        bool onItem = false;
        for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (const UiVirtRow* row = s.ent[p].try_get<UiVirtRow>(); row != nullptr && p != b.wTree) {
                const bool underGrid = uiFiles(s).fileView != kUiNone;
                if (underGrid && row->index < static_cast<uint32_t>(b.count)) {
                    if (b.items[row->index].selected == 0) {
                        b.selectOnly(static_cast<int>(row->index));
                    }
                    onItem = true;
                }
            }
        }
        openFileMenu(&s, in.pointer.x, in.pointer.y, onItem);
        return true;
    }
    return false;
}

bool filesWantsText(const UiState&) { return false; }

} // namespace

void filesMount(UiState& s) { mountFiles(s); }

void filesClick(UiState& s, uint16_t id) {
    Browser& b = book(s);
    if (id == b.plus) {
        onPlus(&s, id);
    } else if (id == b.back) {
        onBack(&s, id);
    } else if (id == b.fwd) {
        onFwd(&s, id);
    }
}

void filesShutdown(UiState& s) {
    delete static_cast<Browser*>(uiFiles(s).files);
    uiFiles(s).files = nullptr;
}

void filesShow(UiState& s, bool on) {
    book(s);
    if (uiFiles(s).fileFrame != kUiNone) {
        uiShow(s, uiFiles(s).fileFrame, on);
    }
    s.visualDirty = true;
}

void filesGo(UiState& s, const char* path) {
    book(s).go(path);
    refresh(s);
}

void filesStyle(UiState& s, const FileLook& look) {
    Browser& b = book(s);
    b.look = look;
    b.showHidden = look.showHidden;
    b.reload();
    refresh(s);
}

void filesOnOpen(UiState& s, FileOpenFn fn, void* user) {
    Browser& b = book(s);
    b.onOpen = fn;
    b.user = user;
}

void filesOfferPath(UiState& s, const char* path) {
    if (path == nullptr || path[0] == '\0' || uiFiles(s).fileTarget == kUiNone) {
        return;
    }
    uiSetFieldText(s, uiFiles(s).fileTarget, path);
}

void filesLoadIcons(Canvas& c) {
    if (c.state == nullptr) {
        return;
    }
    auto loadIcon = [&](uint8_t slot, const char* const* paths, int n) {
        for (int i = 0; i < n; ++i) {
            if (canvasLoadPhoto(c, slot, paths[i])) {
                return true;
            }
        }
        return false;
    };
    const char* folderPaths[] = {"assets/icons/folder.png", "../assets/icons/folder.png"};
    const char* filePaths[] = {"assets/icons/file.png", "../assets/icons/file.png"};
    const bool folderOk = loadIcon(6, folderPaths, 2);
    const bool fileOk = loadIcon(7, filePaths, 2);
    filesIconSlots(*c.state, folderOk ? 6 : 255, fileOk ? 7 : 255);
}

void filesIconSlots(UiState& s, uint8_t folderSlot, uint8_t fileSlot) {
    gFolder = folderSlot;
    gFile = fileSlot;
    if (uiFiles(s).files == nullptr) {
        return;
    }
    Browser& b = book(s);
    b.folderSlot = folderSlot;
    b.fileSlot = fileSlot;
    if (b.mounted != 0) {
        refresh(s);
    }
}

void filesRoot(UiState& s, const char* path) {
    Browser& b = book(s);
    if (path == nullptr || path[0] == '\0') {
        b.root[0] = '\0';
        b.reload();
        refresh(s);
        return;
    }
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(path), ec);
    if (!ec) {
        copyCap(b.root, 512, canon.string().c_str());
        if (!b.underRoot(b.cwd)) {
            b.go(b.root);
        } else {
            b.reload();
        }
    }
    refresh(s);
}

void uiClearDomain(UiState& s) {
    if (auto* files = s.world.try_get_mut<UiFileBrowser>()) {
        files->photoView = kUiNone;
        files->fileView = kUiNone;
        files->fileFrame = kUiNone;
    }
}

void filesAttach(UiState& s) {
    uiClearAdd(s, uiClearDomain);
    s.plug.handle = filesKeys;
    s.plug.draw = nullptr;
    s.plug.over = nullptr;
    s.plug.wantsText = filesWantsText;
    s.plug.shutdown = filesShutdown;
}

} // namespace burnhope
