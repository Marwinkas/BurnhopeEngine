#include "files/Browser.hpp"
#include "files/Format.hpp"
#include "files/Model.hpp"
#include "platform/Desktop.hpp"
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/VirtualGrid.hpp"

#include "ui/Domain.hpp"

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
    uint16_t up = kUiNone;
    uint16_t crumbs = kUiNone;
    uint16_t wTree = kUiNone;
    uint16_t status = kUiNone;
    uint16_t wRename = kUiNone;
    uint8_t ctrl = 0;
    uint8_t shift = 0;
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
            const char* home = std::getenv("HOME");
            copyCap(created->cwd, 512, home != nullptr ? home : ".");
        }
        copyCap(created->hist[0], 512, created->cwd);
        created->histN = 1;
        created->reload();
    }
    return *static_cast<Browser*>(uiFiles(s).files);
}

void refresh(UiState& s);

void openAt(UiState& s, int index) {
    Browser& b = book(s);
    if (index < 0 || index >= b.count) {
        return;
    }
    char path[512]{};
    joinPath(path, 512, b.cwd, b.items[index].name);
    if (b.items[index].isDir != 0) {
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

void onPlus(void* user, uint16_t id, int16_t) {
    UiState& s = *static_cast<UiState*>(user);
    const UiBox& box = s.box[id];
    const UiMenuItem items[] = {
        {.label = "New Folder", .onClick = makeFolder},
        {.label = "New Text Document", .onClick = makeFile},
    };
    uiMenuOpen(s, box.x, box.y + box.h, items, user);
}

void onBack(void* user, uint16_t, int16_t) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).goBack();
    refresh(s);
}

void onFwd(void* user, uint16_t, int16_t) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).goForward();
    refresh(s);
}

void onUp(void* user, uint16_t, int16_t) {
    UiState& s = *static_cast<UiState*>(user);
    char parent[512]{};
    parentOf(book(s).cwd, parent, 512);
    book(s).go(parent);
    refresh(s);
}

void onNav(void* user, const char* path) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).go(path);
    refresh(s);
}

void onTree(void* user, uint32_t index) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    if (index >= static_cast<uint32_t>(b.treeN)) {
        return;
    }
    char path[512]{};
    joinPath(path, 512, b.root[0] != '\0' ? b.root : b.cwd, b.FileModel::tree[index]);
    b.go(path);
    refresh(s);
}

void bindNode(void* user, uint16_t row, uint32_t index) {
    auto* s = static_cast<UiState*>(user);
    Browser& b = book(*s);
    if (index >= static_cast<uint32_t>(b.treeN)) {
        return;
    }
    uiRowIndent(*s, row, static_cast<float>(b.treeDepth[index]) * 14.0f);
    const UiParts* parts = s->ent[row].try_get<UiParts>();
    if (parts == nullptr) {
        return;
    }
    const bool leaf = index + 1 >= static_cast<uint32_t>(b.treeN) || b.treeDepth[index + 1] <= b.treeDepth[index];
    uiShow(*s, parts->chevron, !leaf);
    if (!leaf) {
        uiText(*s, parts->chevron, b.treeOpen[index] != 0 ? "v" : ">");
    }
    uiSetIcon(*s, parts->icon, b.folderSlot);
    uiText(*s, parts->label, b.FileModel::tree[index]);
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
    const bool editing = b.renameAt == static_cast<int>(index);
    uiText(*s, cell, editing ? "" : b.items[index].name);
    if (auto* paint = s->ent[cell].try_get_mut<UiPaint>()) {
        const float* rgb = b.items[index].selected != 0 ? b.look.accent : b.look.card;
        paint->token = static_cast<uint8_t>(UiToken::Custom);
        paint->r = rgb[0];
        paint->g = rgb[1];
        paint->b = rgb[2];
    }
    if (const UiParts* parts = s->ent[cell].try_get<UiParts>()) {
        const uint8_t slot = b.items[index].isDir != 0 ? b.folderSlot : b.fileSlot;
        uiSetIcon(*s, parts->icon, slot);
    }
}

void onSelect(void* user, uint32_t index, bool) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = book(s);
    const int at = static_cast<int>(index);
    if (b.shift != 0) {
        b.selectRange(b.anchor, at);
    } else if (b.ctrl != 0) {
        b.toggleSelect(at);
    } else {
        b.selectOnly(at);
    }
    refresh(s);
}

void onOpen(void* user, uint32_t index) { openAt(*static_cast<UiState*>(user), static_cast<int>(index)); }

void onBand(void* user, uint16_t child) {
    UiState& s = *static_cast<UiState*>(user);
    const UiVirtRow* row = child < s.count ? s.ent[child].try_get<UiVirtRow>() : nullptr;
    Browser& b = book(s);
    if (row != nullptr && row->index < static_cast<uint32_t>(b.count)) {
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

void onZoom(void* user, float value) {
    UiState& s = *static_cast<UiState*>(user);
    book(s).look.icon = value;
    refresh(s);
}

void refresh(UiState& s) {
    Browser& b = book(s);
    b.rebuildTree();
    if (b.crumbs != kUiNone) {
        uiBreadcrumbsSet(s, b.crumbs, b.cwd);
    }
    if (b.wTree != kUiNone && b.wTree < s.count && s.ent[b.wTree].is_alive()) {
        if (auto* tree = s.ent[b.wTree].try_get_mut<UiTreeView>()) {
            tree->count = static_cast<uint32_t>(b.treeN);
            uiTreeViewRefresh(s, b.wTree);
        }
    }
    if (uiFiles(s).fileView != kUiNone && uiFiles(s).fileView < s.count && s.ent[uiFiles(s).fileView].is_alive()) {
        if (auto* grid = s.ent[uiFiles(s).fileView].try_get_mut<UiVirtualGrid>()) {
            grid->count = static_cast<uint32_t>(b.count);
            grid->itemW = b.look.icon;
            grid->itemH = b.look.icon + 28.0f;
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
    row.direction = 1;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = h;
    row.shrink = 0.0f;
    row.align = 1;
    row.gap = 6.0f;
    row.padL = 4.0f;
    row.padR = 4.0f;
    return row;
}

UiPaint ink(UiRole role, float r, float g, float b, int16_t tag = -1) {
    UiPaint out{};
    out.role = static_cast<uint8_t>(role);
    out.r = r;
    out.g = g;
    out.b = b;
    out.a = 1.0f;
    out.tag = tag;
    return out;
}

void mountFiles(UiState& s) {
    if (uiFiles(s).fileFrame == kUiNone || book(s).mounted != 0) {
        return;
    }
    Browser& b = book(s);
    b.mounted = 1;
    const uint16_t bar = uiNode(s, uiFiles(s).fileFrame, rowH(32.0f), ink(UiRole::Hidden, 0, 0, 0));
    b.back = uiButton(s, bar, "<", onBack, &s);
    b.fwd = uiButton(s, bar, ">", onFwd, &s);
    b.up = uiButton(s, bar, "^", onUp, &s);
    b.plus = uiButton(s, bar, "+", onPlus, &s);
    b.crumbs = uiBreadcrumbs(s, bar, onNav, &s);
    (void)uiSearchField(s, bar, "search", onSearch, &s);
    UiFlex body{};
    body.direction = 1;
    body.grow = 1.0f;
    body.shrink = 1.0f;
    body.minH = 80.0f;
    const uint16_t mid = uiNode(s, uiFiles(s).fileFrame, body, ink(UiRole::Hidden, 0, 0, 0));
    UiFlex side{};
    side.widthMode = static_cast<uint8_t>(UiSize::Px);
    side.width = b.look.tree;
    side.heightMode = static_cast<uint8_t>(UiSize::Percent);
    side.height = 100.0f;
    side.shrink = 0.0f;
    const uint16_t pane = uiNode(s, mid, side, ink(UiRole::Panel, b.look.side[0], b.look.side[1], b.look.side[2]));
    UiTreeConfig tree{};
    tree.totalNodes = static_cast<uint32_t>(b.treeN);
    tree.onBindNode = bindNode;
    tree.onToggleNode = onToggle;
    tree.onSelectNode = onTree;
    tree.user = &s;
    b.wTree = uiTreeView(s, pane, tree);
    UiFlex grip{};
    grip.widthMode = static_cast<uint8_t>(UiSize::Px);
    grip.width = 6.0f;
    grip.heightMode = static_cast<uint8_t>(UiSize::Percent);
    grip.height = 100.0f;
    grip.shrink = 0.0f;
    uiNode(s, mid, grip, ink(UiRole::Panel, 0.22f, 0.24f, 0.28f, -5));
    UiFlex gridHost{};
    gridHost.grow = 1.0f;
    gridHost.shrink = 1.0f;
    const uint16_t right = uiNode(s, mid, gridHost, ink(UiRole::Hidden, 0, 0, 0));
    UiVirtualGridConfig cfg{};
    cfg.itemW = b.look.icon;
    cfg.itemH = b.look.icon + 28.0f;
    cfg.totalItems = static_cast<uint32_t>(b.count);
    cfg.onBindCell = bindCell;
    cfg.onSelect = onSelect;
    cfg.onActivate = onOpen;
    cfg.user = &s;
    uiFiles(s).fileView = uiVirtualGrid(s, right, cfg);
    (void)uiRubberband(s, uiFiles(s).fileView, onBand, &s);
    const uint16_t foot = uiNode(s, uiFiles(s).fileFrame, rowH(24.0f), ink(UiRole::Hidden, 0, 0, 0));
    UiFlex label{};
    label.grow = 1.0f;
    label.shrink = 1.0f;
    label.heightMode = static_cast<uint8_t>(UiSize::Px);
    label.height = 18.0f;
    b.status = uiNode(s, foot, label, ink(UiRole::Label, 0.78f, 0.82f, 0.88f));
    uiText(s, b.status, "");
    (void)uiSlider(s, foot, b.look.icon, 52.0f, 148.0f, onZoom, &s, 110.0f);
    UiFlex rename{};
    rename.position = 1;
    rename.widthMode = static_cast<uint8_t>(UiSize::Px);
    rename.heightMode = static_cast<uint8_t>(UiSize::Px);
    rename.width = 160.0f;
    rename.height = 24.0f;
    rename.shrink = 0.0f;
    b.wRename = uiNode(s, uiFiles(s).fileFrame, rename, ink(UiRole::Field, 0.10f, 0.11f, 0.14f));
    if (b.wRename != kUiNone) {
        s.ent[b.wRename].set<UiField>({});
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

bool filesKeys(UiState& s, const InputFrame& in) {
    if (uiFiles(s).files == nullptr || uiFiles(s).fileFrame == kUiNone) {
        return false;
    }
    Browser& b = book(s);
    b.ctrl = in.ctrl;
    b.shift = in.shift;
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
                    uiShow(s, b.wRename, true);
                    uiFocus(s, b.wRename);
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
        onPlus(&s, id, -100);
    } else if (id == b.back) {
        onBack(&s, id, -100);
    } else if (id == b.up) {
        onUp(&s, id, -100);
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
            std::FILE* probe = std::fopen(paths[i], "rb");
            if (probe == nullptr) {
                continue;
            }
            std::fclose(probe);
            if (canvasLoadImage(c, slot, paths[i])) {
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

void filesAttach(UiState& s) {
    s.plug.handle = filesKeys;
    s.plug.draw = nullptr;
    s.plug.over = nullptr;
    s.plug.wantsText = filesWantsText;
    s.plug.shutdown = filesShutdown;
}

} // namespace burnhope
