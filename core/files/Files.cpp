#include "files/Browser.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <spawn.h>

extern char** environ;

namespace burnhope {
namespace {

namespace fs = std::filesystem;

void clickFile(UiState& s, uint16_t id);

void fileButton(void* user, uint16_t id, int16_t) {
    clickFile(*static_cast<UiState*>(user), id);
}

constexpr int kFileCap = 256;
constexpr int kClipCap = 12;
constexpr int kFavCap = 8;
constexpr int kHistCap = 16;

struct Item {
    char name[64]{};
    uint8_t isDir = 0;
    uint8_t kind = 0;
    uint8_t selected = 0;
    uint64_t bytes = 0;
};

struct Browser {
    FileLook look{};
    char cwd[512]{};
    char root[512]{};
    char hist[kHistCap][512]{};
    int histAt = 0;
    int histN = 0;
    Item items[kFileCap]{};
    int count = 0;
    int total = 0;
    int scroll = 0;
    int anchor = 0;
    int hot = -1;
    int drag = -1;
    int dragging = 0;
    float pressX = 0;
    float pressY = 0;
    float dragX = 0;
    float dragY = 0;
    int menu = 0;
    float menuX = 0;
    float menuY = 0;
    int confirm = 0;
    int mode = 0;
    int renameAt = -1;
    char search[40]{};
    char edit[64]{};
    int editAll = 0;
    int focus = 0;
    int hotItem = -1;
    int banding = 0;
    int zooming = 0;
    float bandX0 = 0;
    float bandY0 = 0;
    uint8_t bandBase[kFileCap]{};
    char clip[kClipCap][512]{};
    int clipN = 0;
    int clipCut = 0;
    char fav[kFavCap][512]{};
    int favN = 0;
    char tree[16][64]{};
    int treeN = 0;
    int lastClick = -1;
    uint16_t wSearch = kUiNone;
    uint16_t wRename = kUiNone;
    uint16_t wZoom = kUiNone;
    uint16_t wSide = kUiNone;
    uint16_t wTool[7]{};
    uint16_t wChip[5]{};
    uint16_t wCrumb[8]{};
    uint16_t wArrow[7]{};
    uint16_t wFav[8]{};
    uint16_t wTree[16]{};
    uint16_t wRoot = kUiNone;
    uint16_t wCell[18]{};
    uint16_t wStatus = kUiNone;
    int cellItem[18]{};
    int crumbPart[8]{};
    int kind = 0;
    int split = 0;
    char status[96]{};
    FileOpenFn onOpen = nullptr;
    void* user = nullptr;
};

struct Zone {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

struct Lay {
    Zone tools[7]{};
    Zone zoom{};
    Zone search{};
    Zone chips[5]{};
    Zone crumbs[8]{};
    int crumbAt[8]{};
    int crumbN = 0;
    Zone side{};
    Zone split{};
    Zone grid{};
    Zone detail{};
    Zone foot{};
    Zone cells[40]{};
    int cellId[40]{};
    int cellN = 0;
    Zone sides[32]{};
    int sideId[32]{};
    int sideN = 0;
    Zone menu[12]{};
    int menuN = 0;
    Zone yes{};
    Zone no{};
    int cols = 1;
    int first = 0;
};

void copyCap(char* dst, int cap, const char* src) {
    int i = 0;
    if (src != nullptr) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

bool inside(const Zone& z, float x, float y) {
    return z.w > 1.0f && z.h > 1.0f && x >= z.x && y >= z.y && x < z.x + z.w && y < z.y + z.h;
}

int kindOf(const char* name, int isDir) {
    if (isDir != 0) {
        return 1;
    }
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr) {
        return 0;
    }
    char ext[12]{};
    copyCap(ext, 12, dot);
    for (char* p = ext; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    if (std::strcmp(ext, ".png") == 0 || std::strcmp(ext, ".jpg") == 0 || std::strcmp(ext, ".jpeg") == 0
        || std::strcmp(ext, ".gif") == 0 || std::strcmp(ext, ".bmp") == 0 || std::strcmp(ext, ".tga") == 0
        || std::strcmp(ext, ".hdr") == 0 || std::strcmp(ext, ".psd") == 0) {
        return 2;
    }
    if (std::strcmp(ext, ".txt") == 0 || std::strcmp(ext, ".md") == 0 || std::strcmp(ext, ".json") == 0
        || std::strcmp(ext, ".cpp") == 0 || std::strcmp(ext, ".hpp") == 0 || std::strcmp(ext, ".h") == 0
        || std::strcmp(ext, ".slang") == 0 || std::strcmp(ext, ".cmake") == 0) {
        return 3;
    }
    if (std::strcmp(ext, ".wav") == 0 || std::strcmp(ext, ".ogg") == 0 || std::strcmp(ext, ".mp3") == 0
        || std::strcmp(ext, ".flac") == 0) {
        return 4;
    }
    return 0;
}

const char* kindName(int kind) {
    if (kind == 1) {
        return "Folder";
    }
    if (kind == 2) {
        return "Image";
    }
    if (kind == 3) {
        return "Text";
    }
    if (kind == 4) {
        return "Audio";
    }
    return "File";
}

bool underRoot(const Browser& b, const char* path) {
    if (b.root[0] == '\0' || path == nullptr) {
        return true;
    }
    const size_t n = std::strlen(b.root);
    if (std::strncmp(path, b.root, n) != 0) {
        return false;
    }
    return path[n] == '\0' || path[n] == '/';
}

bool sameText(const char* a, const char* b) {
    return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}

void cachePath(char* dst, int cap) {
    dst[0] = '\0';
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }
    std::snprintf(dst, static_cast<size_t>(cap), "%s/.cache/burnhope-files.txt", home);
}

void saveBook(const Browser& b) {
    char path[640]{};
    cachePath(path, 640);
    if (path[0] == '\0') {
        return;
    }
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "dir %s\n", b.cwd);
    for (int i = 0; i < b.favN; ++i) {
        std::fprintf(file, "fav %s\n", b.fav[i]);
    }
    std::fclose(file);
}

void loadBook(Browser& b) {
    char path[640]{};
    cachePath(path, 640);
    std::FILE* file = path[0] != '\0' ? std::fopen(path, "r") : nullptr;
    if (file != nullptr) {
        char line[640]{};
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            size_t n = std::strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            if (std::strncmp(line, "dir ", 4) == 0 && b.cwd[0] == '\0') {
                copyCap(b.cwd, 512, line + 4);
            } else if (std::strncmp(line, "fav ", 4) == 0 && b.favN < kFavCap) {
                copyCap(b.fav[b.favN], 512, line + 4);
                ++b.favN;
            }
        }
        std::fclose(file);
    }
    if (b.cwd[0] == '\0') {
        const char* home = std::getenv("HOME");
        copyCap(b.cwd, 512, home != nullptr ? home : ".");
    }
    if (b.favN == 0) {
        const char* home = std::getenv("HOME");
        if (home != nullptr && b.favN < kFavCap) {
            copyCap(b.fav[b.favN++], 512, home);
        }
    }
}

void setStatus(Browser& b, const char* text) {
    copyCap(b.status, 96, text);
}

bool searchHit(const Browser& b, const char* name) {
    if (b.search[0] == '\0') {
        return true;
    }
    char a[64]{};
    char c[40]{};
    copyCap(a, 64, name);
    copyCap(c, 40, b.search);
    for (char* p = a; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    for (char* p = c; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    return std::strstr(a, c) != nullptr;
}

void joinPath(char* dst, int cap, const char* dir, const char* name) {
    if (dir != nullptr && dir[0] == '/' && dir[1] == '\0') {
        std::snprintf(dst, static_cast<size_t>(cap), "/%s", name);
        return;
    }
    std::snprintf(dst, static_cast<size_t>(cap), "%s/%s", dir, name);
}

void reload(Browser& b) {
    Item fresh[kFileCap]{};
    int n = 0;
    int total = 0;
    std::error_code ec;
    fs::directory_iterator it(fs::path(b.cwd), fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            const fs::directory_entry& ent = *it;
            std::error_code ignored;
            const bool dir = ent.is_directory(ignored);
            const std::string filename = ent.path().filename().string();
            if (filename.empty() || filename.size() >= 63) {
                continue;
            }
            if (b.look.showHidden == 0 && filename[0] == '.') {
                continue;
            }
            const int kind = kindOf(filename.c_str(), dir ? 1 : 0);
            if (b.kind == 1 && !dir) {
                continue;
            }
            if (b.kind == 2 && kind != 2) {
                continue;
            }
            if (b.kind == 3 && kind != 3) {
                continue;
            }
            if (b.kind == 4 && kind != 4) {
                continue;
            }
            if (!searchHit(b, filename.c_str())) {
                continue;
            }
            ++total;
            if (n >= kFileCap) {
                continue;
            }
            copyCap(fresh[n].name, 64, filename.c_str());
            fresh[n].isDir = dir ? 1 : 0;
            fresh[n].kind = static_cast<uint8_t>(kind);
            if (!dir) {
                fresh[n].bytes = static_cast<uint64_t>(ent.file_size(ignored));
            }
            ++n;
        }
    }
    std::sort(fresh, fresh + n, [](const Item& a, const Item& c) {
        if (a.isDir != c.isDir) {
            return a.isDir > c.isDir;
        }
        return std::strcmp(a.name, c.name) < 0;
    });
    b.count = n;
    b.total = total;
    for (int i = 0; i < n; ++i) {
        b.items[i] = fresh[i];
    }
    if (b.scroll > b.count) {
        b.scroll = 0;
    }
    if (b.status[0] == '\0') {
        char buf[96]{};
        std::snprintf(buf, sizeof(buf), "%d items", b.count);
        setStatus(b, buf);
    }
    b.treeN = 0;
    const char* base = b.root[0] != '\0' ? b.root : b.cwd;
    std::error_code treeEc;
    fs::directory_iterator treeIt(fs::path(base), fs::directory_options::skip_permission_denied, treeEc);
    if (!treeEc) {
        for (; treeIt != fs::directory_iterator() && b.treeN < 16; treeIt.increment(treeEc)) {
            if (treeEc) {
                break;
            }
            std::error_code ignored;
            if (!treeIt->is_directory(ignored)) {
                continue;
            }
            const std::string filename = treeIt->path().filename().string();
            if (filename.empty() || filename.size() >= 63) {
                continue;
            }
            if (b.look.showHidden == 0 && filename[0] == '.') {
                continue;
            }
            copyCap(b.tree[b.treeN], 64, filename.c_str());
            ++b.treeN;
        }
    }
    for (int i = 1; i < b.treeN; ++i) {
        char key[64]{};
        copyCap(key, 64, b.tree[i]);
        int j = i;
        while (j > 0 && std::strcmp(b.tree[j - 1], key) > 0) {
            copyCap(b.tree[j], 64, b.tree[j - 1]);
            --j;
        }
        copyCap(b.tree[j], 64, key);
    }
}

void remember(Browser& b, const char* path) {
    if (path == nullptr || path[0] == '\0' || !underRoot(b, path)) {
        return;
    }
    if (b.histN > 0 && sameText(b.hist[b.histAt], path)) {
        copyCap(b.cwd, 512, path);
        return;
    }
    if (b.histAt + 1 < b.histN) {
        b.histN = b.histAt + 1;
    }
    if (b.histN >= kHistCap) {
        for (int i = 1; i < kHistCap; ++i) {
            copyCap(b.hist[i - 1], 512, b.hist[i]);
        }
        b.histN = kHistCap - 1;
        b.histAt = b.histN - 1;
    }
    copyCap(b.hist[b.histN], 512, path);
    b.histAt = b.histN;
    ++b.histN;
    copyCap(b.cwd, 512, path);
    b.scroll = 0;
    b.anchor = 0;
    b.confirm = 0;
    b.renameAt = -1;
    if (b.mode == 2) {
        b.mode = 0;
    }
    reload(b);
    saveBook(b);
}

void go(Browser& b, const char* path) {
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(path != nullptr ? path : "."), ec);
    if (ec) {
        setStatus(b, "cannot open");
        return;
    }
    const std::string text = canon.string();
    if (!underRoot(b, text.c_str())) {
        setStatus(b, "outside root");
        return;
    }
    std::error_code dirEc;
    if (!fs::is_directory(canon, dirEc)) {
        setStatus(b, "not a folder");
        return;
    }
    remember(b, text.c_str());
}

void parentOf(const char* path, char* dst, int cap) {
    copyCap(dst, cap, path);
    char* slash = std::strrchr(dst, '/');
    if (slash == nullptr) {
        return;
    }
    if (slash == dst) {
        slash[1] = '\0';
        return;
    }
    *slash = '\0';
}

void selectedPaths(Browser& b, char out[][512], int cap, int& n) {
    n = 0;
    for (int i = 0; i < b.count && n < cap; ++i) {
        if (b.items[i].selected == 0 || b.items[i].isDir == 2) {
            continue;
        }
        joinPath(out[n], 512, b.cwd, b.items[i].name);
        ++n;
    }
}

int selectedCount(const Browser& b) {
    int n = 0;
    for (int i = 0; i < b.count; ++i) {
        if (b.items[i].selected != 0) {
            ++n;
        }
    }
    return n;
}

void selectOnly(Browser& b, int index) {
    for (int i = 0; i < b.count; ++i) {
        b.items[i].selected = i == index ? 1 : 0;
    }
    b.anchor = index;
}

void uniquePath(char* dst, int cap, const char* dir, const char* stem, const char* ext) {
    std::snprintf(dst, static_cast<size_t>(cap), "%s/%s%s", dir, stem, ext);
    std::error_code ec;
    int n = 2;
    while (fs::exists(fs::path(dst), ec) && n < 500) {
        std::snprintf(dst, static_cast<size_t>(cap), "%s/%s %d%s", dir, stem, n, ext);
        ++n;
    }
}

void splitStem(const char* name, char* stem, int stemCap, char* ext, int extCap) {
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr || dot == name) {
        copyCap(stem, stemCap, name);
        ext[0] = '\0';
        return;
    }
    const int n = static_cast<int>(dot - name);
    int i = 0;
    while (i < n && i + 1 < stemCap) {
        stem[i] = name[i];
        ++i;
    }
    stem[i] = '\0';
    copyCap(ext, extCap, dot);
}

bool copyOne(const char* src, const char* dst) {
    std::error_code ec;
    const fs::path from(src);
    const fs::path to(dst);
    if (fs::is_directory(from, ec)) {
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    } else {
        fs::copy_file(from, to, fs::copy_options::skip_existing, ec);
    }
    return !ec;
}

void doPaste(Browser& b) {
    int pasted = 0;
    for (int i = 0; i < b.clipN; ++i) {
        std::error_code ec;
        if (!fs::exists(fs::path(b.clip[i]), ec)) {
            continue;
        }
        const fs::path src(b.clip[i]);
        char stem[64]{};
        char ext[16]{};
        splitStem(src.filename().string().c_str(), stem, 64, ext, 16);
        char dst[512]{};
        uniquePath(dst, 512, b.cwd, stem, ext);
        if (b.clipCut != 0) {
            fs::rename(src, fs::path(dst), ec);
            if (ec) {
                continue;
            }
        } else if (!copyOne(b.clip[i], dst)) {
            continue;
        }
        ++pasted;
    }
    if (b.clipCut != 0) {
        b.clipN = 0;
        b.clipCut = 0;
    }
    reload(b);
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "pasted %d", pasted);
    setStatus(b, buf);
}

void doDelete(Browser& b) {
    int removed = 0;
    for (int i = 0; i < b.count; ++i) {
        if (b.items[i].selected == 0) {
            continue;
        }
        char path[512]{};
        joinPath(path, 512, b.cwd, b.items[i].name);
        std::error_code ec;
        fs::remove_all(fs::path(path), ec);
        if (!ec) {
            ++removed;
        }
    }
    b.confirm = 0;
    reload(b);
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "deleted %d", removed);
    setStatus(b, buf);
}

void doDuplicate(Browser& b) {
    char paths[kClipCap][512]{};
    int n = 0;
    selectedPaths(b, paths, kClipCap, n);
    if (n == 0) {
        setStatus(b, "nothing selected");
        return;
    }
    int made = 0;
    for (int i = 0; i < n; ++i) {
        const fs::path src(paths[i]);
        char stem[64]{};
        char ext[16]{};
        splitStem(src.filename().string().c_str(), stem, 64, ext, 16);
        char dst[512]{};
        uniquePath(dst, 512, b.cwd, stem, ext);
        if (copyOne(paths[i], dst)) {
            ++made;
        }
    }
    reload(b);
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "duplicated %d", made);
    setStatus(b, buf);
}

void doRename(Browser& b) {
    if (b.renameAt < 0 || b.renameAt >= b.count || b.edit[0] == '\0') {
        b.mode = 0;
        b.renameAt = -1;
        return;
    }
    char src[512]{};
    char dst[512]{};
    joinPath(src, 512, b.cwd, b.items[b.renameAt].name);
    joinPath(dst, 512, b.cwd, b.edit);
    std::error_code ec;
    if (!sameText(src, dst)) {
        fs::rename(fs::path(src), fs::path(dst), ec);
    }
    b.mode = 0;
    b.renameAt = -1;
    b.editAll = 0;
    reload(b);
    setStatus(b, ec ? "rename failed" : "renamed");
}

void startRename(UiState& s, Browser& b, int selectAll) {
    int pick = -1;
    for (int i = 0; i < b.count; ++i) {
        if (b.items[i].selected != 0) {
            if (pick >= 0) {
                setStatus(b, "select one");
                return;
            }
            pick = i;
        }
    }
    if (pick < 0) {
        return;
    }
    b.mode = 2;
    b.renameAt = pick;
    b.editAll = selectAll;
    b.scroll = pick > 1 ? pick - 1 : 0;
    copyCap(b.edit, 64, b.items[pick].name);
    if (b.wRename != kUiNone) {
        uiSetFieldText(s, b.wRename, b.items[pick].name);
        if (auto* field = s.ent[b.wRename].try_get_mut<UiField>()) {
            field->anchor = selectAll != 0 ? 0 : field->len;
            field->caret = field->len;
        }
        uiShow(s, b.wRename, true);
        uiFocus(s, b.wRename);
        s.editField = b.wRename;
    }
    setStatus(b, "type a name, Enter");
}

void addFav(Browser& b, const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    for (int i = 0; i < b.favN; ++i) {
        if (sameText(b.fav[i], path)) {
            return;
        }
    }
    if (b.favN >= kFavCap) {
        return;
    }
    copyCap(b.fav[b.favN++], 512, path);
    saveBook(b);
    setStatus(b, "favorite");
}

void moveInto(Browser& b, const char* destDir) {
    if (destDir == nullptr || b.drag < 0) {
        return;
    }
    char moving[kClipCap][512]{};
    int n = 0;
    selectedPaths(b, moving, kClipCap, n);
    if (n == 0 && b.drag < b.count) {
        joinPath(moving[0], 512, b.cwd, b.items[b.drag].name);
        n = 1;
    }
    int moved = 0;
    for (int i = 0; i < n; ++i) {
        const fs::path src(moving[i]);
        if (src.parent_path() == fs::path(destDir)) {
            continue;
        }
        char dst[512]{};
        joinPath(dst, 512, destDir, src.filename().string().c_str());
        std::error_code ec;
        fs::rename(src, fs::path(dst), ec);
        if (!ec) {
            ++moved;
        }
    }
    reload(b);
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "moved %d", moved);
    setStatus(b, buf);
}

void newFolder(UiState& s, Browser& b) {
    char dst[512]{};
    uniquePath(dst, 512, b.cwd, "New Folder", "");
    std::error_code ec;
    fs::create_directory(fs::path(dst), ec);
    if (ec) {
        setStatus(b, "create failed");
        uiTrace(s, "folder-fail", s.fileFrame);
        return;
    }
    uiTrace(s, "folder", s.fileFrame);
    reload(b);
    const char* name = std::strrchr(dst, '/');
    name = name != nullptr ? name + 1 : dst;
    for (int i = 0; i < b.count; ++i) {
        if (sameText(b.items[i].name, name)) {
            selectOnly(b, i);
            startRename(s, b, 1);
            b.scroll = i > 1 ? i - 1 : 0;
            break;
        }
    }
    setStatus(b, "new folder");
}

void openItem(UiState& s, Browser& b, int index) {
    if (index < 0 || index >= b.count) {
        return;
    }
    char path[512]{};
    joinPath(path, 512, b.cwd, b.items[index].name);
    if (b.items[index].isDir != 0) {
        go(b, path);
        if (b.onOpen != nullptr) {
            b.onOpen(b.user, b.cwd, 1);
        }
        s.visualDirty = true;
        return;
    }
    char* argv[] = {const_cast<char*>("xdg-open"), path, nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) != 0) {
        setStatus(b, "cannot open");
    }
    if (b.onOpen != nullptr) {
        b.onOpen(b.user, path, 0);
    }
    setStatus(b, b.items[index].name);
    s.visualDirty = true;
}

void copyClip(Browser& b, int cut) {
    b.clipN = 0;
    selectedPaths(b, b.clip, kClipCap, b.clipN);
    b.clipCut = cut;
    setStatus(b, cut != 0 ? "cut" : "copied");
}

bool shown(const UiState& s) {
    if (s.fileFrame == kUiNone || s.fileFrame >= s.count || s.yoga[s.fileFrame] == nullptr) {
        return false;
    }
    return YGNodeStyleGetDisplay(s.yoga[s.fileFrame]) != YGDisplayNone;
}

Browser& book(UiState& s) {
    if (s.files == nullptr) {
        auto* created = new Browser();
        s.files = created;
        loadBook(*created);
        std::error_code ec;
        if (!fs::is_directory(fs::path(created->cwd), ec)) {
            const char* home = std::getenv("HOME");
            copyCap(created->cwd, 512, home != nullptr ? home : ".");
        }
        copyCap(created->hist[0], 512, created->cwd);
        created->histN = 1;
        created->histAt = 0;
        if (created->root[0] == '\0') {
            std::error_code rootEc;
            const fs::path canon = fs::weakly_canonical(fs::path(created->cwd), rootEc);
            copyCap(created->root, 512, rootEc ? created->cwd : canon.string().c_str());
            copyCap(created->cwd, 512, created->root);
            copyCap(created->hist[0], 512, created->root);
        }
        reload(*created);
    }
    return *static_cast<Browser*>(s.files);
}

float textWidth(const UiState& s, const char* text);
const char* toolName(int i);
const char* chipName(int i);

void layout(const UiState& s, const Browser& b, Lay& lay) {
    lay = {};
    if (s.fileView == kUiNone || s.fileView >= s.count) {
        return;
    }
    const UiBox& box = s.box[s.fileView];
    lay.foot = {box.x + 8.0f, box.y + box.h - 28.0f, box.w > 16.0f ? box.w - 16.0f : 0.0f, 24.0f};
    lay.grid = {box.x + 4.0f, box.y + 4.0f, box.w > 8.0f ? box.w - 8.0f : 0.0f, box.h > 40.0f ? box.h - 36.0f : 0.0f};
    const float icon = b.look.icon < 52.0f ? 52.0f : (b.look.icon > 148.0f ? 148.0f : b.look.icon);
    const bool list = icon < 64.0f;
    lay.cols = list ? 1 : static_cast<int>(lay.grid.w / (icon + 12.0f));
    if (lay.cols < 1) {
        lay.cols = 1;
    }
    const float cellH = list ? 28.0f : icon + 28.0f;
    const int rows = static_cast<int>(lay.grid.h / cellH);
    int vis = rows * lay.cols;
    if (vis > 18) {
        vis = 18;
    }
    int first = b.scroll;
    if (first < 0) {
        first = 0;
    }
    const int maxFirst = b.count > vis ? b.count - vis : 0;
    if (first > maxFirst) {
        first = maxFirst;
    }
    lay.first = first;
    const float cellW = list ? lay.grid.w - 8.0f : (icon + 8.0f);
    for (int i = 0; i < vis && first + i < b.count; ++i) {
        const int col = list ? 0 : i % lay.cols;
        const int row = list ? i : i / lay.cols;
        lay.cells[lay.cellN] = {lay.grid.x + 4.0f + static_cast<float>(col) * (cellW + 4.0f), lay.grid.y + 4.0f + static_cast<float>(row) * cellH, cellW, cellH - 4.0f};
        lay.cellId[lay.cellN] = first + i;
        ++lay.cellN;
    }
    if (b.menu != 0) {
        float mx = b.menuX;
        float my = b.menuY;
        const float mw = 168.0f;
        const float mh = 24.0f * 10.0f;
        if (s.laidW > mw + 8.0f && mx + mw > s.laidW) {
            mx = s.laidW - mw - 4.0f;
        }
        if (s.laidH > mh + 8.0f && my + mh > s.laidH) {
            my = s.laidH - mh - 4.0f;
        }
        if (mx < 4.0f) {
            mx = 4.0f;
        }
        if (my < 4.0f) {
            my = 4.0f;
        }
        for (int i = 0; i < 10; ++i) {
            lay.menu[i] = {mx, my, mw, 24.0f};
            my += 24.0f;
        }
        lay.menuN = 10;
    }
    if (b.confirm != 0) {
        lay.yes = {lay.foot.x + lay.foot.w - 132.0f, lay.foot.y, 60.0f, 22.0f};
        lay.no = {lay.foot.x + lay.foot.w - 66.0f, lay.foot.y, 60.0f, 22.0f};
    }
    auto zoneOf = [&](uint16_t id) -> Zone {
        if (id >= s.count) {
            return {};
        }
        const UiBox& box = s.box[id];
        return {box.x, box.y, box.w, box.h};
    };
    for (int i = 0; i < 7; ++i) {
        lay.tools[i] = zoneOf(b.wTool[i]);
    }
    lay.zoom = zoneOf(b.wZoom);
    lay.search = zoneOf(b.wSearch);
    for (int i = 0; i < 5; ++i) {
        lay.chips[i] = zoneOf(b.wChip[i]);
    }
    lay.crumbN = 0;
    for (int i = 0; i < 8; ++i) {
        const Zone crumb = zoneOf(b.wCrumb[i]);
        if (crumb.w < 1.0f || crumb.h < 1.0f) {
            continue;
        }
        lay.crumbs[lay.crumbN] = crumb;
        lay.crumbAt[lay.crumbN] = b.crumbPart[i];
        ++lay.crumbN;
    }
    lay.split = zoneOf(b.wSide != kUiNone ? b.wSide : kUiNone);
    if (b.wSide < s.count) {
        const UiBox& side = s.box[b.wSide];
        lay.split = {side.x + side.w, side.y, 8.0f, side.h};
        lay.sideN = 0;
        if (b.wRoot < s.count && lay.sideN < 32) {
            lay.sides[lay.sideN] = zoneOf(b.wRoot);
            lay.sideId[lay.sideN] = -3;
            ++lay.sideN;
        }
        for (int i = 0; i < b.favN && i < 8 && lay.sideN < 32; ++i) {
            lay.sides[lay.sideN] = zoneOf(b.wFav[i]);
            lay.sideId[lay.sideN] = i;
            ++lay.sideN;
        }
        for (int i = 0; i < b.treeN && i < 16 && lay.sideN < 32; ++i) {
            lay.sides[lay.sideN] = zoneOf(b.wTree[i]);
            lay.sideId[lay.sideN] = 1000 + i;
            ++lay.sideN;
        }
    }
}

int crumbIndex(const Browser& b, int visible) {
    int parts = 1;
    for (int i = 0; b.cwd[i] != '\0'; ++i) {
        if (b.cwd[i] == '/' && b.cwd[i + 1] != '\0') {
            ++parts;
        }
    }
    const int begin = parts > 4 ? parts - 4 : 0;
    return begin + visible;
}

void crumbPath(const Browser& b, int index, char* dst, int cap) {
    copyCap(dst, cap, b.cwd);
    int part = 0;
    for (int i = 0; dst[i] != '\0'; ++i) {
        if (dst[i] == '/' && dst[i + 1] != '\0') {
            ++part;
            if (part > index) {
                dst[i] = '\0';
                if (dst[0] == '\0') {
                    dst[0] = '/';
                    dst[1] = '\0';
                }
                return;
            }
        }
    }
}

void putFloat(uint32_t& dst, float v) {
    std::memcpy(&dst, &v, sizeof(float));
}

uint32_t boxRect(UiPrimitive* dst, uint32_t n, uint32_t cap, const Zone& z, float r, float g, float b, float a, float radius, const Zone* clip) {
    if (n >= cap || z.w < 0.5f || z.h < 0.5f || a <= 0.0f) {
        return n;
    }
    UiPrimitive& p = dst[n];
    p = {};
    p.posX = z.x;
    p.posY = z.y;
    p.sizeX = z.w;
    p.sizeY = z.h;
    p.color[0] = r;
    p.color[1] = g;
    p.color[2] = b;
    p.color[3] = a;
    p.extra[0] = radius;
    if (clip != nullptr) {
        p.flags |= kUiFlagClip;
        p.extra[1] = clip->x;
        p.extra[2] = clip->y;
        p.extra[3] = clip->w;
        putFloat(p.pad0, clip->h);
    }
    return n + 1;
}

float textWidth(const UiState& s, const char* text) {
    uint8_t len = 0;
    if (text != nullptr) {
        while (text[len] != '\0' && len < 80) {
            ++len;
        }
    }
    return uiMeasure(s, text, len, len);
}

uint32_t label(UiState& s, UiPrimitive* dst, uint32_t n, uint32_t cap, const char* text, const Zone& z, float r, float g, float b, bool center, bool dots = true) {
    if (text == nullptr) {
        return n;
    }
    UiText line{};
    uint8_t len = 0;
    while (text[len] != '\0' && len < 95) {
        line.bytes[len] = text[len];
        ++len;
    }
    line.len = len;
    line.align = center ? 1 : 0;
    line.ellipsis = dots ? 1 : 0;
    const float pad = center ? 0.0f : 4.0f;
    const UiBox slot{z.x + pad, z.y, z.w > pad * 2.0f ? z.w - pad * 2.0f : z.w, z.h};
    const TextRun run = textRun(s, line, slot);
    const UiBox clip{z.x, z.y, z.w, z.h};
    return uiEmitRun(s, dst, n, cap, run, r, g, b, 1.0f, &clip);
}

uint32_t iconOf(UiPrimitive* dst, uint32_t n, uint32_t cap, const Zone& z, int kind, const FileLook& look) {
    const float s = z.w < z.h ? z.w : z.h;
    Zone glyph{z.x + (z.w - s) * 0.5f, z.y, s, s * 0.72f};
    if (kind == 1) {
        n = boxRect(dst, n, cap, glyph, look.folder[0], look.folder[1], look.folder[2], 1.0f, 4.0f, nullptr);
        Zone tab{glyph.x + 2.0f, glyph.y - 4.0f, s * 0.42f, 6.0f};
        return boxRect(dst, n, cap, tab, look.folder[0] * 0.8f, look.folder[1] * 0.8f, look.folder[2] * 0.75f, 1.0f, 2.0f, nullptr);
    }
    float r = 0.45f;
    float g = 0.55f;
    float b = 0.70f;
    if (kind == 2) {
        r = 0.35f;
        g = 0.62f;
        b = 0.95f;
    } else if (kind == 3) {
        r = 0.82f;
        g = 0.84f;
        b = 0.88f;
    } else if (kind == 4) {
        r = 0.45f;
        g = 0.78f;
        b = 0.55f;
    }
    return boxRect(dst, n, cap, glyph, r, g, b, 1.0f, 4.0f, nullptr);
}

const char* toolName(int i) {
    static const char* names[] = {"<", ">", "UP", "NEW", "VIEW", ".*", "GO"};
    return names[i];
}

const char* chipName(int i) {
    static const char* names[] = {"ALL", "DIR", "IMG", "TXT", "SND"};
    return names[i];
}

const char* menuName(int i) {
    static const char* names[] = {"Open", "Rename", "Copy", "Cut", "Paste", "Duplicate", "Delete", "New folder", "Favorite", "In folder"};
    return names[i];
}

void sizeText(uint64_t bytes, char* dst, int cap) {
    if (bytes < 1024) {
        std::snprintf(dst, static_cast<size_t>(cap), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ull * 1024ull) {
        std::snprintf(dst, static_cast<size_t>(cap), "%llu KB", static_cast<unsigned long long>(bytes / 1024ull));
    } else {
        std::snprintf(dst, static_cast<size_t>(cap), "%llu MB", static_cast<unsigned long long>(bytes / (1024ull * 1024ull)));
    }
}

void spawnLine(const char* file, char* const* argv) {
    pid_t pid = 0;
    posix_spawnp(&pid, file, nullptr, nullptr, argv, environ);
}

void fileUri(const char* path, char* dst, int cap) {
    int n = 0;
    const char* prefix = "file://";
    while (prefix[n] != '\0' && n + 1 < cap) {
        dst[n] = prefix[n];
        ++n;
    }
    for (int i = 0; path != nullptr && path[i] != '\0' && n + 4 < cap; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            dst[n++] = static_cast<char>(c);
        } else {
            std::snprintf(dst + n, static_cast<size_t>(cap - n), "%%%02X", c);
            n += 3;
        }
    }
    dst[n] = '\0';
}

void revealSelection(Browser& b) {
    char paths[8][512]{};
    int n = 0;
    selectedPaths(b, paths, 8, n);
    if (n == 0) {
        copyCap(paths[0], 512, b.cwd);
        n = 1;
    }
    char list[2048]{};
    int at = 0;
    for (int i = 0; i < n && at + 8 < static_cast<int>(sizeof(list)); ++i) {
        char uri[640]{};
        fileUri(paths[i], uri, 640);
        const int wrote = std::snprintf(list + at, sizeof(list) - static_cast<size_t>(at), "%s\"%s\"", i == 0 ? "" : ",", uri);
        if (wrote < 0) {
            break;
        }
        at += wrote;
    }
    char parent[512]{};
    parentOf(paths[0], parent, 512);
    char script[2400]{};
    std::snprintf(
        script,
        sizeof(script),
        "dbus-send --session --dest=org.freedesktop.FileManager1 --type=method_call "
        "/org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItems array:string:%s string:\"\" "
        "|| xdg-open \"%s\"",
        list,
        parent);
    char* argv[] = {const_cast<char*>("sh"), const_cast<char*>("-c"), script, nullptr};
    spawnLine("sh", argv);
    setStatus(b, "shown in folder");
}

void menuAct(UiState& s, Browser& b, int item) {
    b.menu = 0;
    if (item == 0) {
        for (int i = 0; i < b.count; ++i) {
            if (b.items[i].selected != 0) {
                openItem(s, b, i);
                return;
            }
        }
    } else if (item == 1) {
        startRename(s, b, 0);
    } else if (item == 2) {
        copyClip(b, 0);
    } else if (item == 3) {
        copyClip(b, 1);
    } else if (item == 4) {
        doPaste(b);
    } else if (item == 5) {
        doDuplicate(b);
    } else if (item == 6) {
        b.confirm = 1;
        setStatus(b, "delete? Enter or YES");
    } else if (item == 7) {
        newFolder(s, b);
    } else if (item == 8) {
        char paths[1][512]{};
        int n = 0;
        selectedPaths(b, paths, 1, n);
        if (n == 1) {
            addFav(b, paths[0]);
        } else {
            addFav(b, b.cwd);
        }
    } else if (item == 9) {
        revealSelection(b);
    }
    s.visualDirty = true;
}

void destroyBook(UiState& s) {
    delete static_cast<Browser*>(s.files);
    s.files = nullptr;
}

const Browser& viewBook(const UiState& s) {
    return *static_cast<const Browser*>(s.files);
}

void typeEdit(Browser& b, const InputFrame& in) {
    char* buf = b.mode == 1 ? b.search : b.edit;
    const int cap = b.mode == 1 ? 39 : 63;
    if (b.mode == 2 && b.editAll != 0 && (in.textLen != 0 || in.pasteOn != 0 || in.backspace != 0)) {
        buf[0] = '\0';
        b.editAll = 0;
    }
    int len = static_cast<int>(std::strlen(buf));
    if (in.backspace != 0 && len > 0) {
        buf[--len] = '\0';
    }
    for (uint8_t i = 0; i < in.textLen && len < cap; ++i) {
        buf[len++] = in.text[i];
    }
    buf[len] = '\0';
    if (in.pasteOn != 0) {
        for (uint8_t i = 0; i < in.pasteLen && len < cap; ++i) {
            buf[len++] = in.paste[i];
        }
        buf[len] = '\0';
    }
}

bool hitSpan(const Zone& z, float x0, float y0, float x1, float y1) {
    const float left = x0 < x1 ? x0 : x1;
    const float right = x0 < x1 ? x1 : x0;
    const float top = y0 < y1 ? y0 : y1;
    const float bottom = y0 < y1 ? y1 : y0;
    return z.w > 1.0f && z.x < right && left < z.x + z.w && z.y < bottom && top < z.y + z.h;
}

void applyBand(Browser& b, const Lay& lay, float x, float y, int add) {
    for (int i = 0; i < b.count; ++i) {
        b.items[i].selected = add != 0 ? b.bandBase[i] : 0;
    }
    for (int i = 0; i < lay.cellN; ++i) {
        if (!hitSpan(lay.cells[i], b.bandX0, b.bandY0, x, y)) {
            continue;
        }
        const int id = lay.cellId[i];
        if (id >= 0 && id < b.count) {
            b.items[id].selected = 1;
        }
    }
}

void applyZoom(Browser& b, const Lay& lay, float x) {
    if (lay.zoom.w < 2.0f) {
        return;
    }
    float t = (x - lay.zoom.x) / lay.zoom.w;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    b.look.icon = 52.0f + t * (148.0f - 52.0f);
}

void writeZoom(UiState& s, Browser& b) {
    if (b.wZoom >= s.count) {
        return;
    }
    if (auto* range = s.ent[b.wZoom].try_get_mut<UiRange>()) {
        range->value = b.look.icon;
    }
}

void readZoom(UiState& s, Browser& b) {
    if (b.wZoom >= s.count) {
        return;
    }
    if (const UiRange* range = s.ent[b.wZoom].try_get<UiRange>()) {
        b.look.icon = range->value;
    }
}

void describeItem(const Item& item, char* dst, int cap) {
    char ext[16] = "FILE";
    if (item.isDir != 0) {
        copyCap(ext, 16, "DIR");
    } else {
        const char* dot = std::strrchr(item.name, '.');
        if (dot != nullptr && dot != item.name && dot[1] != '\0') {
            int n = 0;
            for (const char* p = dot + 1; *p != '\0' && n < 12; ++p) {
                char c = *p;
                if (c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                }
                ext[n++] = c;
            }
            ext[n] = '\0';
        }
    }
    if (item.isDir != 0) {
        std::snprintf(dst, static_cast<size_t>(cap), "%s (%s)", item.name, ext);
        return;
    }
    char size[32]{};
    sizeText(item.bytes, size, 32);
    std::snprintf(dst, static_cast<size_t>(cap), "%s (%s, %s)", item.name, ext, size);
}

const char* leafOf(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return "";
    }
    const char* slash = std::strrchr(path, '/');
    return (slash != nullptr && slash[1] != '\0') ? slash + 1 : path;
}

void commitRename(UiState& s, Browser& b) {
    if (b.mode != 2) {
        return;
    }
    if (b.wRename != kUiNone && b.wRename < s.count) {
        if (const UiField* field = s.ent[b.wRename].try_get<UiField>()) {
            int n = 0;
            while (n < 63 && n < field->len) {
                b.edit[n] = field->bytes[n];
                ++n;
            }
            b.edit[n] = '\0';
        }
        uiShow(s, b.wRename, false);
        if (s.focused == b.wRename) {
            uiFocus(s, kUiNone);
            s.editField = kUiNone;
        }
    }
    doRename(b);
}

void showNode(UiState& s, uint16_t id, bool on) {
    if (id == kUiNone || id >= s.count || s.yoga[id] == nullptr) {
        return;
    }
    const bool cur = YGNodeStyleGetDisplay(s.yoga[id]) != YGDisplayNone;
    if (cur != on) {
        uiShow(s, id, on);
    }
}

void putText(UiState& s, uint16_t id, const char* text, bool fit) {
    if (id == kUiNone || id >= s.count) {
        return;
    }
    auto* label = s.ent[id].try_get_mut<UiText>();
    if (label == nullptr) {
        return;
    }
    char next[32]{};
    int n = 0;
    if (text != nullptr) {
        while (text[n] != '\0' && n < 31) {
            next[n] = text[n];
            ++n;
        }
    }
    if (label->len == static_cast<uint8_t>(n) && std::memcmp(label->bytes, next, static_cast<size_t>(n)) == 0) {
        return;
    }
    for (int i = 0; i < n; ++i) {
        label->bytes[i] = next[i];
    }
    label->bytes[n] = '\0';
    label->len = static_cast<uint8_t>(n);
    if (fit) {
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            float w = uiMeasure(s, label->bytes, label->len, label->len) + 22.0f;
            if (w < 28.0f) {
                w = 28.0f;
            }
            flex->widthMode = static_cast<uint8_t>(UiSize::Px);
            flex->width = w;
            flex->shrink = 0.0f;
        }
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

void paintBtn(UiState& s, uint16_t id, float r, float g, float bcol) {
    auto* paint = id < s.count ? s.ent[id].try_get_mut<UiPaint>() : nullptr;
    if (paint == nullptr || (paint->r == r && paint->g == g && paint->b == bcol)) {
        return;
    }
    paint->r = r;
    paint->g = g;
    paint->b = bcol;
    s.visualDirty = true;
}

void pullFields(UiState& s, Browser& b) {
    if (b.wSearch != kUiNone && b.wSearch < s.count) {
        if (const UiField* field = s.ent[b.wSearch].try_get<UiField>()) {
            if (!sameText(field->bytes, b.search)) {
                copyCap(b.search, 40, field->bytes);
                b.scroll = 0;
                reload(b);
                s.visualDirty = true;
            }
        }
    }
    if (b.wZoom != kUiNone && b.wZoom < s.count) {
        if (const UiRange* range = s.ent[b.wZoom].try_get<UiRange>()) {
            if (range->value != b.look.icon) {
                b.look.icon = range->value;
                s.visualDirty = true;
            }
        }
    }
}

void bindChrome(UiState& s, Browser& b, const Lay& lay) {
    if (b.wSearch == kUiNone) {
        return;
    }
    char path[512]{};
    copyCap(path, 512, b.cwd);
    int parts[16]{};
    int partN = 0;
    parts[partN++] = 0;
    for (int i = 0; path[i] != '\0' && partN < 16; ++i) {
        if (path[i] == '/' && path[i + 1] != '\0') {
            parts[partN++] = i + 1;
        }
    }
    int rootParts = 1;
    for (int i = 0; b.root[i] != '\0'; ++i) {
        if (b.root[i] == '/' && b.root[i + 1] != '\0') {
            ++rootParts;
        }
    }
    int begin = rootParts > 0 ? rootParts - 1 : 0;
    if (begin >= partN) {
        begin = partN > 0 ? partN - 1 : 0;
    }
    int shown = 0;
    for (int i = begin; i < partN && shown < 8; ++i) {
        const char* text = i == 0 ? "/" : path + parts[i];
        char piece[32]{};
        int n = 0;
        while (text[n] != '\0' && text[n] != '/' && n < 31) {
            piece[n] = text[n];
            ++n;
        }
        piece[n] = '\0';
        putText(s, b.wCrumb[shown], piece, true);
        showNode(s, b.wCrumb[shown], true);
        b.crumbPart[shown] = i;
        const bool last = shown + 1 >= 8 || i + 1 >= partN;
        if (shown < 7) {
            showNode(s, b.wArrow[shown], !last);
        }
        ++shown;
    }
    for (int i = shown; i < 8; ++i) {
        showNode(s, b.wCrumb[i], false);
        if (i < 7) {
            showNode(s, b.wArrow[i], false);
        }
    }
    const char* root = b.root[0] != '\0' ? b.root : b.cwd;
    putText(s, b.wRoot, leafOf(root), false);
    paintBtn(s, b.wRoot, sameText(root, b.cwd) ? 0.28f : 0.14f, sameText(root, b.cwd) ? 0.48f : 0.16f, sameText(root, b.cwd) ? 0.92f : 0.20f);
    for (int i = 0; i < 8; ++i) {
        const bool on = i < b.favN;
        showNode(s, b.wFav[i], on);
        if (!on) {
            continue;
        }
        putText(s, b.wFav[i], leafOf(b.fav[i]), false);
        const bool here = sameText(b.fav[i], b.cwd);
        paintBtn(s, b.wFav[i], here ? 0.28f : 0.14f, here ? 0.48f : 0.16f, here ? 0.92f : 0.20f);
    }
    for (int i = 0; i < 16; ++i) {
        const bool on = i < b.treeN;
        showNode(s, b.wTree[i], on);
        if (!on) {
            continue;
        }
        putText(s, b.wTree[i], b.tree[i], false);
        char full[512]{};
        joinPath(full, 512, root, b.tree[i]);
        const bool here = sameText(full, b.cwd);
        paintBtn(s, b.wTree[i], here ? 0.28f : 0.14f, here ? 0.48f : 0.16f, here ? 0.92f : 0.20f);
    }
    for (int i = 0; i < 5; ++i) {
        const bool on = b.kind == i;
        paintBtn(s, b.wChip[i], on ? 0.28f : 0.16f, on ? 0.48f : 0.18f, on ? 0.92f : 0.22f);
    }
    for (int i = 0; i < 18; ++i) {
        const bool on = i < lay.cellN;
        b.cellItem[i] = on ? lay.cellId[i] : -1;
        showNode(s, b.wCell[i], on);
        if (!on || b.wCell[i] >= s.count) {
            continue;
        }
        const Item& item = b.items[lay.cellId[i]];
        putText(s, b.wCell[i], item.name, false);
        if (item.selected != 0) {
            paintBtn(s, b.wCell[i], b.look.accent[0], b.look.accent[1], b.look.accent[2]);
        } else if (item.isDir != 0) {
            paintBtn(s, b.wCell[i], b.look.folder[0], b.look.folder[1], b.look.folder[2]);
        } else {
            paintBtn(s, b.wCell[i], b.look.card[0], b.look.card[1], b.look.card[2]);
        }
        const Zone& cell = lay.cells[i];
        const float lx = cell.x - s.box[s.fileView].x;
        const float ly = cell.y - s.box[s.fileView].y;
        auto* flex = s.ent[b.wCell[i]].try_get_mut<UiFlex>();
        if (flex != nullptr && (flex->position == 0 || std::fabs(flex->posX - lx) > 0.5f || std::fabs(flex->posY - ly) > 0.5f || std::fabs(flex->width - cell.w) > 0.5f)) {
            uiPlace(s, b.wCell[i], true, lx, ly, cell.w, cell.h);
        }
    }
    if (b.wStatus != kUiNone) {
        char foot[32]{};
        const int picked = selectedCount(b);
        if (b.status[0] != '\0') {
            putText(s, b.wStatus, b.status, false);
        } else if (picked > 1) {
            std::snprintf(foot, sizeof(foot), "%d selected", picked);
            putText(s, b.wStatus, foot, false);
        } else {
            std::snprintf(foot, sizeof(foot), "%d items", b.count);
            putText(s, b.wStatus, foot, false);
        }
    }
    if (b.mode == 2 && b.wRename != kUiNone) {
        for (int i = 0; i < lay.cellN; ++i) {
            if (lay.cellId[i] != b.renameAt) {
                continue;
            }
            const Zone& cell = lay.cells[i];
            auto* flex = s.ent[b.wRename].try_get_mut<UiFlex>();
            if (flex != nullptr && (std::fabs(flex->posX - cell.x) > 0.5f || std::fabs(flex->posY - cell.y) > 0.5f || std::fabs(flex->width - cell.w) > 0.5f)) {
                uiPlace(s, b.wRename, true, cell.x, cell.y, cell.w, 28.0f);
            }
        }
    }
}

void toolAct(UiState& s, Browser& b, int i) {
    if (i == 0 && b.histAt > 0) {
        --b.histAt;
        if (underRoot(b, b.hist[b.histAt])) {
            copyCap(b.cwd, 512, b.hist[b.histAt]);
            reload(b);
        }
    } else if (i == 1 && b.histAt + 1 < b.histN) {
        ++b.histAt;
        if (underRoot(b, b.hist[b.histAt])) {
            copyCap(b.cwd, 512, b.hist[b.histAt]);
            reload(b);
        }
    } else if (i == 2) {
        char parent[512]{};
        parentOf(b.cwd, parent, 512);
        if (sameText(parent, b.cwd) || sameText(b.cwd, b.root)) {
            setStatus(b, "root");
        } else {
            go(b, parent);
        }
    } else if (i == 3) {
        newFolder(s, b);
    } else if (i == 4) {
        b.look.icon = b.look.icon < 64.0f ? 88.0f : 56.0f;
        if (b.wZoom != kUiNone) {
            if (auto* range = s.ent[b.wZoom].try_get_mut<UiRange>()) {
                range->value = b.look.icon;
            }
        }
    } else if (i == 5) {
        b.look.showHidden = b.look.showHidden != 0 ? 0 : 1;
        reload(b);
    } else if (i == 6) {
        reload(b);
    }
    s.visualDirty = true;
}

void clickFile(UiState& s, uint16_t id) {
    Browser& b = book(s);
    if (b.mode == 2) {
        commitRename(s, b);
    }
    for (int i = 0; i < 7; ++i) {
        if (b.wTool[i] == id) {
            toolAct(s, b, i);
            return;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (b.wChip[i] == id) {
            b.kind = i;
            b.scroll = 0;
            reload(b);
            s.visualDirty = true;
            return;
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (b.wCrumb[i] == id) {
            char path[512]{};
            crumbPath(b, b.crumbPart[i], path, 512);
            go(b, path);
            s.visualDirty = true;
            return;
        }
    }
    const char* root = b.root[0] != '\0' ? b.root : b.cwd;
    for (int i = 0; i < 8; ++i) {
        if (b.wFav[i] == id && i < b.favN) {
            go(b, b.fav[i]);
            s.visualDirty = true;
            return;
        }
    }
    if (b.wRoot == id) {
        go(b, root);
        s.visualDirty = true;
        return;
    }
    for (int i = 0; i < 18; ++i) {
        if (b.wCell[i] != id || b.cellItem[i] < 0) {
            continue;
        }
        const int cell = b.cellItem[i];
        if (b.lastClick == cell) {
            b.lastClick = -1;
            openItem(s, b, cell);
        } else {
            selectOnly(b, cell);
            b.lastClick = cell;
            b.focus = 1;
        }
        s.visualDirty = true;
        return;
    }
    for (int i = 0; i < 16; ++i) {
        if (b.wTree[i] == id && i < b.treeN) {
            char dest[512]{};
            joinPath(dest, 512, root, b.tree[i]);
            go(b, dest);
            s.visualDirty = true;
            return;
        }
    }
}

void actFile(void* user, int item) {
    UiState& s = *static_cast<UiState*>(user);
    menuAct(s, book(s), item);
}
void actFile0(void* user) { actFile(user, 0); }
void actFile1(void* user) { actFile(user, 1); }
void actFile2(void* user) { actFile(user, 2); }
void actFile3(void* user) { actFile(user, 3); }
void actFile4(void* user) { actFile(user, 4); }
void actFile5(void* user) { actFile(user, 5); }
void actFile6(void* user) { actFile(user, 6); }
void actFile7(void* user) { actFile(user, 7); }
void actFile8(void* user) { actFile(user, 8); }
void actFile9(void* user) { actFile(user, 9); }

UiPaint solidPaint(UiRole role, float r, float g, float b, float radius, int8_t tag) {
    UiPaint paint{};
    paint.r = r;
    paint.g = g;
    paint.b = b;
    paint.a = 1.0f;
    paint.radius = radius;
    paint.role = static_cast<uint8_t>(role);
    paint.tag = tag;
    return paint;
}

UiFlex rowBox(float h) {
    UiFlex flex{};
    flex.direction = 1;
    flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    flex.width = 100.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = h;
    flex.shrink = 0.0f;
    flex.gap = 6.0f;
    flex.align = 1;
    return flex;
}

uint16_t makeButton(UiState& s, uint16_t parent, const char* text, int8_t tag, float h, bool wide) {
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = h;
    flex.shrink = 0.0f;
    flex.align = 1;
    flex.justify = 1;
    if (wide) {
        flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex.width = 100.0f;
    }
    const uint16_t id = uiNode(s, parent, flex, solidPaint(UiRole::Button, 0.16f, 0.18f, 0.22f, 6.0f, tag));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text);
    uiBind(s, id, fileButton, &s);
    if (!wide) {
        if (auto* box = s.ent[id].try_get_mut<UiFlex>()) {
            const UiText* label = s.ent[id].try_get<UiText>();
            float w = label != nullptr ? uiMeasure(s, label->bytes, label->len, label->len) + 22.0f : 36.0f;
            if (w < 28.0f) {
                w = 28.0f;
            }
            box->widthMode = static_cast<uint8_t>(UiSize::Px);
            box->width = w;
            box->heightMode = static_cast<uint8_t>(UiSize::Px);
            box->height = h;
            box->shrink = 0.0f;
        }
    }
    return id;
}

void mountFiles(UiState& s) {
    if (s.fileFrame == kUiNone) {
        return;
    }
    Browser& b = book(s);
    const char* tools[] = {"<", ">", "UP", "NEW", "VIEW", ".*", "GO"};
    const uint16_t toolRow = uiNode(s, s.fileFrame, rowBox(30.0f), solidPaint(UiRole::Hidden, 0, 0, 0, 0, -1));
    for (int i = 0; i < 7; ++i) {
        b.wTool[i] = makeButton(s, toolRow, tools[i], -100, 28.0f, false);
    }
    UiFlex zoom{};
    zoom.grow = 1.0f;
    zoom.shrink = 1.0f;
    zoom.heightMode = static_cast<uint8_t>(UiSize::Px);
    zoom.height = 18.0f;
    b.wZoom = uiNode(s, toolRow, zoom, solidPaint(UiRole::Slider, 0.18f, 0.20f, 0.26f, 8.0f, -1));
    if (b.wZoom != kUiNone) {
        s.ent[b.wZoom].set<UiRange>({b.look.icon, 52.0f, 148.0f});
    }
    UiFlex search{};
    search.widthMode = static_cast<uint8_t>(UiSize::Px);
    search.width = 180.0f;
    search.heightMode = static_cast<uint8_t>(UiSize::Px);
    search.height = 28.0f;
    search.shrink = 0.0f;
    b.wSearch = uiNode(s, toolRow, search, solidPaint(UiRole::Field, 0.10f, 0.11f, 0.14f, 4.0f, -1));
    if (b.wSearch != kUiNone) {
        UiField field{};
        copyCap(field.hint, 40, "search");
        field.hintLen = 6;
        s.ent[b.wSearch].set<UiField>(field);
    }
    const uint16_t chipRow = uiNode(s, s.fileFrame, rowBox(28.0f), solidPaint(UiRole::Hidden, 0, 0, 0, 0, -1));
    const char* chips[] = {"ALL", "DIR", "IMG", "TXT", "SND"};
    for (int i = 0; i < 5; ++i) {
        b.wChip[i] = makeButton(s, chipRow, chips[i], -100, 26.0f, false);
    }
    const uint16_t crumbRow = uiNode(s, s.fileFrame, rowBox(26.0f), solidPaint(UiRole::Hidden, 0, 0, 0, 0, -1));
    for (int i = 0; i < 8; ++i) {
        b.wCrumb[i] = makeButton(s, crumbRow, "", -100, 24.0f, false);
        if (i < 7) {
            UiFlex arrow{};
            arrow.shrink = 0.0f;
            b.wArrow[i] = uiNode(s, crumbRow, arrow, solidPaint(UiRole::Label, 0.45f, 0.50f, 0.58f, 0.0f, -1));
            if (b.wArrow[i] != kUiNone) {
                uiText(s, b.wArrow[i], ">");
            }
        }
    }
    UiFlex body{};
    body.direction = 1;
    body.widthMode = static_cast<uint8_t>(UiSize::Percent);
    body.width = 100.0f;
    body.grow = 1.0f;
    body.shrink = 1.0f;
    body.gap = 0.0f;
    const uint16_t bodyId = uiNode(s, s.fileFrame, body, solidPaint(UiRole::Hidden, 0, 0, 0, 0, -1));
    UiFlex side{};
    side.widthMode = static_cast<uint8_t>(UiSize::Px);
    side.width = b.look.tree < 140.0f ? 188.0f : b.look.tree;
    side.shrink = 0.0f;
    side.pad = 6.0f;
    side.gap = 2.0f;
    b.wSide = uiNode(s, bodyId, side, solidPaint(UiRole::Panel, b.look.side[0], b.look.side[1], b.look.side[2], 6.0f, -1));
    const uint16_t favHead = uiNode(s, b.wSide, UiFlex{}, solidPaint(UiRole::Label, 0.50f, 0.55f, 0.62f, 0.0f, -1));
    uiText(s, favHead, "FAVORITES");
    for (int i = 0; i < 8; ++i) {
        b.wFav[i] = makeButton(s, b.wSide, "", -100, 24.0f, true);
        showNode(s, b.wFav[i], false);
    }
    const uint16_t dirHead = uiNode(s, b.wSide, UiFlex{}, solidPaint(UiRole::Label, 0.50f, 0.55f, 0.62f, 0.0f, -1));
    uiText(s, dirHead, "FOLDERS");
    b.wRoot = makeButton(s, b.wSide, "root", -100, 24.0f, true);
    for (int i = 0; i < 16; ++i) {
        b.wTree[i] = makeButton(s, b.wSide, "", -100, 24.0f, true);
        showNode(s, b.wTree[i], false);
    }
    UiFlex split{};
    split.widthMode = static_cast<uint8_t>(UiSize::Px);
    split.width = 8.0f;
    split.shrink = 0.0f;
    uiNode(s, bodyId, split, solidPaint(UiRole::Panel, 0.28f, 0.32f, 0.40f, 0.0f, -5));
    UiFlex grid{};
    grid.grow = 1.0f;
    grid.shrink = 1.0f;
    grid.position = 2;
    s.fileView = uiNode(s, bodyId, grid, solidPaint(UiRole::Panel, b.look.grid[0], b.look.grid[1], b.look.grid[2], 6.0f, -91));
    for (int i = 0; i < 18; ++i) {
        UiFlex cell{};
        cell.position = 1;
        cell.widthMode = static_cast<uint8_t>(UiSize::Px);
        cell.width = 84.0f;
        cell.heightMode = static_cast<uint8_t>(UiSize::Px);
        cell.height = 84.0f;
        cell.shrink = 0.0f;
        b.wCell[i] = uiNode(s, s.fileView, cell, solidPaint(UiRole::Button, b.look.card[0], b.look.card[1], b.look.card[2], 6.0f, -1));
        if (b.wCell[i] != kUiNone) {
            uiText(s, b.wCell[i], "");
            showNode(s, b.wCell[i], false);
        }
    }
    b.wStatus = uiNode(s, s.fileFrame, rowBox(22.0f), solidPaint(UiRole::Label, 0.78f, 0.82f, 0.88f, 0.0f, -1));
    if (b.wStatus != kUiNone) {
        uiText(s, b.wStatus, "");
    }
    UiFlex menu{};
    menu.position = 1;
    menu.widthMode = static_cast<uint8_t>(UiSize::Px);
    menu.width = s.menuW;
    menu.pad = s.menuPad;
    menu.gap = s.menuGap;
    menu.shrink = 0.0f;
    const uint16_t popup = uiNode(s, 0, menu, solidPaint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f, -1));
    if (popup != kUiNone && s.yoga[popup] != nullptr) {
        YGNodeStyleSetDisplay(s.yoga[popup], YGDisplayNone);
        s.contextOf[s.fileView] = popup;
        uiCommandAddTo(s, popup, "Open", actFile0, &s);
        uiCommandAddTo(s, popup, "Rename", actFile1, &s);
        uiCommandAddTo(s, popup, "Copy", actFile2, &s);
        uiCommandAddTo(s, popup, "Cut", actFile3, &s);
        uiCommandAddTo(s, popup, "Paste", actFile4, &s);
        uiCommandAddTo(s, popup, "Duplicate", actFile5, &s);
        uiCommandAddTo(s, popup, "Delete", actFile6, &s);
        uiCommandAddTo(s, popup, "New folder", actFile7, &s);
        uiCommandAddTo(s, popup, "Favorite", actFile8, &s);
        uiCommandAddTo(s, popup, "In folder", actFile9, &s);
    }
    UiFlex rename{};
    rename.position = 1;
    rename.widthMode = static_cast<uint8_t>(UiSize::Px);
    rename.width = 160.0f;
    rename.heightMode = static_cast<uint8_t>(UiSize::Px);
    rename.height = 28.0f;
    rename.shrink = 0.0f;
    b.wRename = uiNode(s, 0, rename, solidPaint(UiRole::Field, 0.10f, 0.11f, 0.14f, 4.0f, -1));
    if (b.wRename != kUiNone) {
        UiField field{};
        field.padL = 6.0f;
        field.padR = 6.0f;
        s.ent[b.wRename].set<UiField>(field);
        uiShow(s, b.wRename, false);
    }
}

} // namespace

void filesMount(UiState& s) {
    mountFiles(s);
}

void filesClick(UiState& s, uint16_t id) {
    clickFile(s, id);
}

bool filesWantsText(const UiState& s) {
    if (!shown(s) || s.files == nullptr) {
        return false;
    }
    const Browser& b = viewBook(s);
    if (b.wSearch != kUiNone) {
        return false;
    }
    return b.focus != 0 && (b.mode == 1 || b.mode == 2);
}

void filesShutdown(UiState& s) {
    destroyBook(s);
}

void filesShow(UiState& s, bool on) {
    book(s);
    if (s.fileFrame != kUiNone) {
        uiShow(s, s.fileFrame, on);
    }
    if (on) {
        book(s).focus = 1;
    }
    s.visualDirty = true;
}

void filesGo(UiState& s, const char* path) {
    go(book(s), path);
    s.visualDirty = true;
}

void filesStyle(UiState& s, const FileLook& look) {
    book(s).look = look;
    reload(book(s));
    s.visualDirty = true;
}

void filesOnOpen(UiState& s, FileOpenFn fn, void* user) {
    Browser& b = book(s);
    b.onOpen = fn;
    b.user = user;
}

void filesRoot(UiState& s, const char* path) {
    Browser& b = book(s);
    if (path == nullptr || path[0] == '\0') {
        b.root[0] = '\0';
        return;
    }
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(path), ec);
    if (!ec) {
        copyCap(b.root, 512, canon.string().c_str());
        if (!underRoot(b, b.cwd)) {
            go(b, b.root);
        }
    }
}

uint32_t filesGesture(UiState& s, const Browser& b, const Lay& lay, UiPrimitive* dst, uint32_t n, uint32_t cap);

bool cellsMounted(const UiState& s, const Browser& b) {
    return b.wCell[0] != 0 && b.wCell[0] != kUiNone && b.wCell[0] < s.count;
}

bool hoverIsFileWidget(const UiState& s, const Browser& b) {
    if (s.hovered == kUiNone || s.hovered >= s.count) {
        return false;
    }
    auto owns = [&](uint16_t id) {
        if (id == kUiNone || id >= s.count) {
            return false;
        }
        for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (p == id) {
                return true;
            }
        }
        return false;
    };
    for (int i = 0; i < 18; ++i) {
        if (owns(b.wCell[i]) || (i < 8 && owns(b.wFav[i])) || (i < 8 && owns(b.wCrumb[i])) || (i < 7 && owns(b.wTool[i]))) {
            return true;
        }
    }
    for (int i = 0; i < 16; ++i) {
        if (owns(b.wTree[i])) {
            return true;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (owns(b.wChip[i])) {
            return true;
        }
    }
    return owns(b.wRoot) || owns(b.wSearch) || owns(b.wZoom) || owns(b.wRename) || owns(b.wStatus);
}

bool filesHandle(UiState& s, const InputFrame& in) {
    if (!shown(s)) {
        return false;
    }
    Browser& b = book(s);
    readZoom(s, b);
    pullFields(s, b);
    if (b.mode != 0 && b.wRename == kUiNone) {
        s.visualDirty = true;
    }
    Lay lay{};
    layout(s, b, lay);
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    const bool right = (in.pointer.pressed & kPointerRight) != 0;
    const float x = in.pointer.x;
    const float y = in.pointer.y;
    const bool over = s.hovered == s.fileView || s.capture == s.fileView || b.menu != 0;
    const bool fieldFocus = s.focused != kUiNone && s.focused < s.count && s.ent[s.focused].try_get<UiField>() != nullptr;
    if (b.mode == 2 && b.wRename != kUiNone && in.enter != 0 && s.focused == b.wRename) {
        commitRename(s, b);
        layout(s, b, lay);
    } else if (press && b.mode == 2 && b.wRename != kUiNone && s.hovered != b.wRename) {
        commitRename(s, b);
        layout(s, b, lay);
    }
    bindChrome(s, b, lay);
    if (!fieldFocus && b.focus != 0 && b.mode != 0 && b.wSearch == kUiNone && (in.textLen != 0 || in.backspace != 0 || in.pasteOn != 0)) {
        typeEdit(b, in);
        if (b.mode == 1) {
            b.scroll = 0;
            reload(b);
        }
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 1 && in.enter != 0) {
        b.mode = 0;
        s.visualDirty = true;
    } else if (!fieldFocus && b.focus != 0 && b.mode == 2 && in.enter != 0) {
        doRename(b);
        s.visualDirty = true;
    } else if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.enter != 0 && b.confirm != 0) {
        doDelete(b);
        s.visualDirty = true;
    } else if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.enter != 0) {
        for (int i = 0; i < b.count; ++i) {
            if (b.items[i].selected != 0) {
                openItem(s, b, i);
                break;
            }
        }
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.f2 != 0) {
        startRename(s, b, 0);
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.del != 0) {
        if (b.confirm != 0) {
            doDelete(b);
        } else if (selectedCount(b) > 0) {
            b.confirm = 1;
            setStatus(b, "delete? Enter or YES");
        }
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.selectAll != 0) {
        for (int i = 0; i < b.count; ++i) {
            b.items[i].selected = 1;
        }
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.copy != 0) {
        copyClip(b, 0);
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.cut != 0) {
        copyClip(b, 1);
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.pasteOn != 0) {
        doPaste(b);
        s.visualDirty = true;
    }
    if (!fieldFocus && b.focus != 0 && b.mode == 0 && in.backspace != 0) {
        char parent[512]{};
        parentOf(b.cwd, parent, 512);
        go(b, parent);
        s.visualDirty = true;
    }
    int hot = -1;
    for (int i = 0; i < lay.sideN; ++i) {
        if (lay.sideId[i] != -1 && lay.sideId[i] != -2 && inside(lay.sides[i], x, y)) {
            hot = i;
        }
    }
    if (hot != b.hot) {
        b.hot = hot;
        s.visualDirty = true;
    }
    int hotItem = -1;
    for (int i = 0; i < lay.cellN; ++i) {
        if (inside(lay.cells[i], x, y)) {
            hotItem = lay.cellId[i];
        }
    }
    if (hotItem != b.hotItem) {
        b.hotItem = hotItem;
        s.visualDirty = true;
    }
    if (!over && !press && !right && !down && !release && in.wheel == 0.0f) {
        return false;
    }
    if (in.wheel != 0.0f && inside(lay.grid, x, y)) {
        if (in.ctrl != 0) {
            b.look.icon += in.wheel * 8.0f;
            if (b.look.icon < 52.0f) {
                b.look.icon = 52.0f;
            }
            if (b.look.icon > 148.0f) {
                b.look.icon = 148.0f;
            }
            if (b.wZoom != kUiNone) {
                if (auto* range = s.ent[b.wZoom].try_get_mut<UiRange>()) {
                    range->value = b.look.icon;
                }
            }
        } else {
            b.scroll -= static_cast<int>(in.wheel) * (lay.cols > 0 ? lay.cols : 1);
            if (b.scroll < 0) {
                b.scroll = 0;
            }
        }
        s.visualDirty = true;
        return false;
    }
    if (right && (s.hovered == s.fileView || inside(lay.grid, x, y))) {
        int cell = -1;
        for (int i = 0; i < lay.cellN; ++i) {
            if (inside(lay.cells[i], x, y)) {
                cell = lay.cellId[i];
            }
        }
        if (cell >= 0 && b.items[cell].selected == 0) {
            selectOnly(b, cell);
        }
        b.focus = 1;
        s.visualDirty = true;
        return false;
    }
    if (!press && down && s.capture == s.fileView) {
        if (b.zooming != 0) {
            applyZoom(b, lay, x);
            writeZoom(s, b);
            s.visualDirty = true;
            return false;
        }
        if (b.banding != 0) {
            b.dragX = x;
            b.dragY = y;
            applyBand(b, lay, x, y, in.ctrl);
            s.visualDirty = true;
            return false;
        }
        if (b.split != 0) {
            const float local = x - s.box[s.fileView].x - 8.0f;
            b.look.tree = local < 120.0f ? 120.0f : (local > 360.0f ? 360.0f : local);
            s.visualDirty = true;
            return false;
        }
        const float dx = x - b.pressX;
        const float dy = y - b.pressY;
        b.dragX = x;
        b.dragY = y;
        if (b.drag >= 0 && dx * dx + dy * dy > 25.0f) {
            b.dragging = 1;
            s.visualDirty = true;
        }
        return false;
    }
    if (release && b.dragging != 0) {
        bool moved = false;
        for (int i = 0; i < lay.crumbN && !moved; ++i) {
            if (!inside(lay.crumbs[i], x, y)) {
                continue;
            }
            char dest[512]{};
            crumbPath(b, lay.crumbAt[i], dest, 512);
            moveInto(b, dest);
            moved = true;
        }
        for (int i = 0; i < lay.cellN && !moved; ++i) {
            if (!inside(lay.cells[i], x, y)) {
                continue;
            }
            const int id = lay.cellId[i];
            if (b.items[id].isDir != 0) {
                char dest[512]{};
                joinPath(dest, 512, b.cwd, b.items[id].name);
                moveInto(b, dest);
                moved = true;
            }
        }
        for (int i = 0; i < lay.sideN && !moved; ++i) {
            if (!inside(lay.sides[i], x, y)) {
                continue;
            }
            const char* root = b.root[0] != '\0' ? b.root : b.cwd;
            if (lay.sideId[i] == -3) {
                moveInto(b, root);
                moved = true;
            } else if (lay.sideId[i] >= 0 && lay.sideId[i] < 1000) {
                moveInto(b, b.fav[lay.sideId[i]]);
                moved = true;
            } else if (lay.sideId[i] >= 1000 && lay.sideId[i] - 1000 < b.treeN) {
                char dest[512]{};
                joinPath(dest, 512, root, b.tree[lay.sideId[i] - 1000]);
                moveInto(b, dest);
                moved = true;
            }
        }
        auto hitNode = [&](uint16_t id) {
            return id != kUiNone && id < s.count && s.yoga[id] != nullptr && YGNodeStyleGetDisplay(s.yoga[id]) != YGDisplayNone
                && x >= s.box[id].x && y >= s.box[id].y && x < s.box[id].x + s.box[id].w && y < s.box[id].y + s.box[id].h;
        };
        const char* root = b.root[0] != '\0' ? b.root : b.cwd;
        for (int i = 0; i < 8 && !moved; ++i) {
            if (!hitNode(b.wCrumb[i]) || YGNodeStyleGetDisplay(s.yoga[b.wCrumb[i]]) == YGDisplayNone) {
                continue;
            }
            char dest[512]{};
            crumbPath(b, b.crumbPart[i], dest, 512);
            moveInto(b, dest);
            moved = true;
        }
        if (!moved && hitNode(b.wRoot)) {
            moveInto(b, root);
            moved = true;
        }
        for (int i = 0; i < 8 && !moved; ++i) {
            if (!hitNode(b.wFav[i])) {
                continue;
            }
            moveInto(b, b.fav[i]);
            moved = true;
        }
        for (int i = 0; i < 16 && !moved; ++i) {
            if (!hitNode(b.wTree[i])) {
                continue;
            }
            char dest[512]{};
            joinPath(dest, 512, root, b.tree[i]);
            moveInto(b, dest);
            moved = true;
        }
        b.dragging = 0;
        b.drag = -1;
        s.visualDirty = true;
    }
    if (release) {
        b.split = 0;
        b.zooming = 0;
        b.banding = 0;
    }
    if (!press) {
        return false;
    }
    if (b.menu != 0) {
        for (int i = 0; i < lay.menuN; ++i) {
            if (inside(lay.menu[i], x, y)) {
                b.focus = 1;
                menuAct(s, b, i);
                return true;
            }
        }
        b.menu = 0;
        s.visualDirty = true;
    }
    const UiBox& frameBox = s.box[s.fileFrame];
    const float edge = 8.0f;
    const bool onFrameEdge = x < frameBox.x + edge || y < frameBox.y + edge || x > frameBox.x + frameBox.w - edge
        || y > frameBox.y + frameBox.h - edge;
    const UiBox& viewBox = s.box[s.fileView];
    const bool inView = x >= viewBox.x && y >= viewBox.y && x < viewBox.x + viewBox.w && y < viewBox.y + viewBox.h;
    const bool inFrame = x >= frameBox.x && y >= frameBox.y && x < frameBox.x + frameBox.w && y < frameBox.y + frameBox.h;
    if (onFrameEdge || !inView) {
        if (!inFrame) {
            b.focus = 0;
            if (b.mode == 2) {
                commitRename(s, b);
            } else {
                b.mode = 0;
            }
        }
        return false;
    }
    if (press && cellsMounted(s, b) && hoverIsFileWidget(s, b) && in.shift == 0 && in.ctrl == 0) {
        b.focus = 1;
        return false;
    }
    b.focus = 1;
    uiFocus(s, kUiNone);
    s.capture = s.fileView;
    b.pressX = x;
    b.pressY = y;
    if (b.confirm != 0 && inside(lay.yes, x, y)) {
        doDelete(b);
        s.visualDirty = true;
        return true;
    }
    if (b.confirm != 0 && inside(lay.no, x, y)) {
        b.confirm = 0;
        setStatus(b, "kept");
        s.visualDirty = true;
        return true;
    }
    if (inside(lay.zoom, x, y)) {
        b.zooming = 1;
        applyZoom(b, lay, x);
        writeZoom(s, b);
        s.visualDirty = true;
        return true;
    }
    for (int i = 0; i < 7; ++i) {
        if (!inside(lay.tools[i], x, y)) {
            continue;
        }
        if (i == 0 && b.histAt > 0) {
            --b.histAt;
            if (underRoot(b, b.hist[b.histAt])) {
                copyCap(b.cwd, 512, b.hist[b.histAt]);
                reload(b);
            }
        } else if (i == 1 && b.histAt + 1 < b.histN) {
            ++b.histAt;
            if (underRoot(b, b.hist[b.histAt])) {
                copyCap(b.cwd, 512, b.hist[b.histAt]);
                reload(b);
            }
        } else if (i == 2) {
            char parent[512]{};
            parentOf(b.cwd, parent, 512);
            if (sameText(parent, b.cwd) || sameText(b.cwd, b.root)) {
                setStatus(b, "root");
            } else {
                go(b, parent);
            }
        } else if (i == 3) {
            newFolder(s, b);
        } else if (i == 4) {
            b.look.icon = b.look.icon < 64.0f ? 88.0f : 56.0f;
        } else if (i == 5) {
            b.look.showHidden = b.look.showHidden != 0 ? 0 : 1;
            reload(b);
        } else if (i == 6) {
            reload(b);
        }
        if (b.look.icon < 52.0f) {
            b.look.icon = 52.0f;
        }
        if (b.look.icon > 148.0f) {
            b.look.icon = 148.0f;
        }
        s.visualDirty = true;
        return true;
    }
    if (inside(lay.search, x, y)) {
        b.mode = 1;
        s.visualDirty = true;
        return true;
    }
    if (b.mode == 1) {
        b.mode = 0;
    }
    for (int i = 0; i < 5; ++i) {
        if (inside(lay.chips[i], x, y)) {
            b.kind = i;
            b.scroll = 0;
            reload(b);
            s.visualDirty = true;
            return true;
        }
    }
    for (int i = 0; i < lay.crumbN; ++i) {
        if (!inside(lay.crumbs[i], x, y)) {
            continue;
        }
        char path[512]{};
        crumbPath(b, lay.crumbAt[i], path, 512);
        go(b, path);
        s.visualDirty = true;
        return true;
    }
    if (inside(lay.split, x, y)) {
        b.split = 1;
        return true;
    }
    for (int i = 0; i < lay.sideN; ++i) {
        if (!inside(lay.sides[i], x, y)) {
            continue;
        }
        const char* root = b.root[0] != '\0' ? b.root : b.cwd;
        if (lay.sideId[i] == -3) {
            go(b, root);
        } else if (lay.sideId[i] >= 0 && lay.sideId[i] < 1000) {
            go(b, b.fav[lay.sideId[i]]);
        } else if (lay.sideId[i] >= 1000 && lay.sideId[i] - 1000 < b.treeN) {
            char dest[512]{};
            joinPath(dest, 512, root, b.tree[lay.sideId[i] - 1000]);
            go(b, dest);
        }
        s.visualDirty = true;
        return true;
    }
    int cell = -1;
    for (int i = 0; i < lay.cellN; ++i) {
        if (inside(lay.cells[i], x, y)) {
            cell = lay.cellId[i];
        }
    }
    if (cell >= 0) {
        if (in.clicks >= 2 && b.lastClick == cell) {
            b.lastClick = -1;
            openItem(s, b, cell);
            return true;
        }
        b.lastClick = cell;
        if (in.shift != 0) {
            const int a = b.anchor < cell ? b.anchor : cell;
            const int c = b.anchor < cell ? cell : b.anchor;
            for (int i = 0; i < b.count; ++i) {
                b.items[i].selected = (i >= a && i <= c) ? 1 : 0;
            }
        } else if (in.ctrl != 0) {
            b.items[cell].selected = b.items[cell].selected != 0 ? 0 : 1;
            b.anchor = cell;
        } else {
            selectOnly(b, cell);
        }
        b.drag = cell;
        b.dragging = 0;
        s.visualDirty = true;
        return true;
    }
    if (inside(lay.grid, x, y)) {
        b.banding = 1;
        b.bandX0 = x;
        b.bandY0 = y;
        b.dragX = x;
        b.dragY = y;
        b.drag = -1;
        for (int i = 0; i < b.count; ++i) {
            b.bandBase[i] = b.items[i].selected;
        }
        b.lastClick = -1;
        if (in.ctrl == 0) {
            selectOnly(b, -1);
        }
        s.visualDirty = true;
    }
    return true;
}

uint32_t filesEmit(UiState& s, UiPrimitive* dst, uint32_t n, uint32_t cap) {
    if (!shown(s) || s.files == nullptr || s.fileView == kUiNone) {
        return n;
    }
    const Browser& b = viewBook(s);
    Lay lay{};
    layout(s, b, lay);
    const UiBox& view = s.box[s.fileView];
    Zone all{view.x, view.y, view.w, view.h};
    if (!cellsMounted(s, b)) {
    n = boxRect(dst, n, cap, all, b.look.grid[0], b.look.grid[1], b.look.grid[2], 1.0f, 8.0f, nullptr);
    if (b.wSearch == kUiNone) {
    for (int i = 0; i < 7; ++i) {
        n = boxRect(dst, n, cap, lay.tools[i], 0.16f, 0.18f, 0.22f, 1.0f, 4.0f, nullptr);
        n = label(s, dst, n, cap, toolName(i), lay.tools[i], 0.9f, 0.92f, 0.95f, true, false);
    }
    if (lay.zoom.w > 2.0f) {
        n = boxRect(dst, n, cap, lay.zoom, 0.1f, 0.11f, 0.14f, 1.0f, 4.0f, nullptr);
        const float span = 148.0f - 52.0f;
        float t = (b.look.icon - 52.0f) / span;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        Zone fill{lay.zoom.x, lay.zoom.y, lay.zoom.w * t, lay.zoom.h};
        n = boxRect(dst, n, cap, fill, b.look.accent[0], b.look.accent[1], b.look.accent[2], 1.0f, 4.0f, nullptr);
        Zone knob{lay.zoom.x + lay.zoom.w * t - 5.0f, lay.zoom.y - 3.0f, 10.0f, 16.0f};
        n = boxRect(dst, n, cap, knob, 0.92f, 0.94f, 0.96f, 1.0f, 4.0f, nullptr);
    }
    const bool typing = b.mode == 1;
    n = boxRect(
        dst, n, cap, lay.search,
        typing ? 0.16f : 0.08f, typing ? 0.2f : 0.09f, typing ? 0.28f : 0.11f, 1.0f, 4.0f, nullptr);
    char searchText[48]{};
    const bool caret = std::fmod(s.time, 1.0f) < 0.55f;
    if (typing) {
        std::snprintf(searchText, sizeof(searchText), "%s%s", b.search, caret ? "|" : "");
    } else if (b.search[0] != '\0') {
        copyCap(searchText, 48, b.search);
    } else {
        copyCap(searchText, 48, "search");
    }
    Zone searchLabel = lay.search;
    searchLabel.x += 8.0f;
    searchLabel.w -= 12.0f;
    n = label(s, dst, n, cap, searchText, searchLabel, 0.9f, 0.92f, 0.96f, false);
    for (int i = 0; i < 5; ++i) {
        const bool on = b.kind == i;
        n = boxRect(dst, n, cap, lay.chips[i], on ? b.look.accent[0] : 0.14f, on ? b.look.accent[1] : 0.15f, on ? b.look.accent[2] : 0.18f, 1.0f, 4.0f, nullptr);
        n = label(s, dst, n, cap, chipName(i), lay.chips[i], 0.93f, 0.94f, 0.96f, true, false);
    }
    for (int i = 0; i < lay.crumbN; ++i) {
        char path[512]{};
        crumbPath(b, lay.crumbAt[i], path, 512);
        const char* slash = std::strrchr(path, '/');
        const char* text = (slash != nullptr && slash[1] != '\0') ? slash + 1 : "/";
        const bool here = std::strcmp(path, b.cwd) == 0;
        const float cr = here ? 0.95f : 0.72f;
        n = label(s, dst, n, cap, text, lay.crumbs[i], cr, here ? 0.96f : 0.78f, here ? 0.98f : 0.84f, false, false);
        if (b.dragging != 0) {
            Zone bar{lay.crumbs[i].x, lay.crumbs[i].y + lay.crumbs[i].h - 2.0f, lay.crumbs[i].w, 2.0f};
            n = boxRect(dst, n, cap, bar, b.look.accent[0], b.look.accent[1], b.look.accent[2], 1.0f, 0.0f, nullptr);
        }
        if (i + 1 < lay.crumbN) {
            Zone arrow{lay.crumbs[i].x + lay.crumbs[i].w + 2.0f, lay.crumbs[i].y, 14.0f, lay.crumbs[i].h};
            n = label(s, dst, n, cap, ">", arrow, 0.45f, 0.5f, 0.58f, false, false);
        }
    }
    if (lay.side.w > 1.0f) {
        n = boxRect(dst, n, cap, lay.side, b.look.side[0], b.look.side[1], b.look.side[2], 1.0f, 6.0f, nullptr);
        Zone gutter{lay.split.x + 3.0f, lay.split.y + 8.0f, 2.0f, lay.split.h - 16.0f};
        n = boxRect(dst, n, cap, gutter, 0.28f, 0.32f, 0.38f, 1.0f, 1.0f, nullptr);
        const char* root = b.root[0] != '\0' ? b.root : b.cwd;
        for (int i = 0; i < lay.sideN; ++i) {
            const int id = lay.sideId[i];
            if (id == -1 || id == -2) {
                n = label(s, dst, n, cap, id == -1 ? "FAVORITES" : "FOLDERS", lay.sides[i], 0.5f, 0.55f, 0.62f, false, false);
                continue;
            }
            char rowPath[512]{};
            const char* text = "";
            if (id == -3) {
                copyCap(rowPath, 512, root);
                const char* slash = std::strrchr(root, '/');
                text = (slash != nullptr && slash[1] != '\0') ? slash + 1 : root;
            } else if (id >= 0 && id < 1000) {
                copyCap(rowPath, 512, b.fav[id]);
                const char* slash = std::strrchr(b.fav[id], '/');
                text = (slash != nullptr && slash[1] != '\0') ? slash + 1 : b.fav[id];
            } else if (id >= 1000 && id - 1000 < b.treeN) {
                joinPath(rowPath, 512, root, b.tree[id - 1000]);
                text = b.tree[id - 1000];
            }
            const bool here = rowPath[0] != '\0' && std::strcmp(rowPath, b.cwd) == 0;
            const bool hot = b.hot == i;
            if (here || hot) {
                n = boxRect(
                    dst, n, cap, lay.sides[i],
                    here ? b.look.accent[0] : 0.16f, here ? b.look.accent[1] : 0.18f, here ? b.look.accent[2] : 0.22f,
                    1.0f, 4.0f, nullptr);
            }
            Zone row = lay.sides[i];
            row.x += id >= 1000 ? 16.0f : 4.0f;
            row.w -= id >= 1000 ? 20.0f : 8.0f;
            n = label(s, dst, n, cap, text, row, 0.9f, 0.92f, 0.95f, false);
        }
    }
    }
    }
    if (!cellsMounted(s, b)) {
    n = boxRect(dst, n, cap, lay.grid, 0.05f, 0.055f, 0.07f, 1.0f, 6.0f, nullptr);
    const bool list = b.look.icon < 64.0f;
    if (b.wCell[0] == kUiNone) {
    for (int i = 0; i < lay.cellN; ++i) {
        const Item& item = b.items[lay.cellId[i]];
        const bool on = item.selected != 0;
        const bool drop = b.dragging != 0 && item.isDir != 0;
        n = boxRect(
            dst, n, cap, lay.cells[i],
            on ? b.look.accent[0] : (drop ? 0.2f : b.look.card[0]),
            on ? b.look.accent[1] : (drop ? 0.28f : b.look.card[1]),
            on ? b.look.accent[2] : (drop ? 0.2f : b.look.card[2]),
            on ? 0.85f : 1.0f, 6.0f, nullptr);
        Zone icon = lay.cells[i];
        Zone name = lay.cells[i];
        if (list) {
            icon.w = 22.0f;
            icon.h = 18.0f;
            icon.y += 2.0f;
            name.x += 26.0f;
            name.w -= 26.0f;
        } else {
            icon.h = lay.cells[i].h - 22.0f;
            name.y += icon.h;
            name.h = 20.0f;
        }
        n = iconOf(dst, n, cap, icon, item.kind, b.look);
        const bool renaming = b.mode == 2 && b.renameAt == lay.cellId[i];
        if (renaming) {
            const bool marked = b.editAll != 0;
            n = boxRect(
                dst, n, cap, name,
                marked ? b.look.accent[0] : 0.08f, marked ? b.look.accent[1] : 0.1f, marked ? b.look.accent[2] : 0.16f,
                marked ? 0.85f : 1.0f, 3.0f, nullptr);
        }
        char title[72]{};
        if (renaming) {
            const bool caret = std::fmod(s.time, 1.0f) < 0.55f;
            std::snprintf(title, sizeof(title), "%s%s", b.edit, caret ? "|" : "");
        } else {
            copyCap(title, 72, item.name);
        }
        n = label(s, dst, n, cap, title, name, 0.92f, 0.93f, 0.95f, !list && !renaming);
    }
    }
    }
    char foot[180]{};
    const int picked = selectedCount(b);
    if (b.confirm != 0 && b.status[0] != '\0') {
        copyCap(foot, 180, b.status);
    } else if (b.hotItem >= 0 && b.hotItem < b.count) {
        describeItem(b.items[b.hotItem], foot, 180);
    } else if (picked == 1) {
        for (int i = 0; i < b.count; ++i) {
            if (b.items[i].selected != 0) {
                describeItem(b.items[i], foot, 180);
                break;
            }
        }
    } else if (picked > 1) {
        uint64_t bytes = 0;
        for (int i = 0; i < b.count; ++i) {
            if (b.items[i].selected != 0 && b.items[i].isDir == 0) {
                bytes += b.items[i].bytes;
            }
        }
        char size[32]{};
        sizeText(bytes, size, 32);
        std::snprintf(foot, sizeof(foot), "%d selected (%s)", picked, size);
    } else if (b.status[0] != '\0') {
        copyCap(foot, 180, b.status);
    } else {
        std::snprintf(foot, sizeof(foot), "%d items", b.count);
    }
    if (b.wStatus == kUiNone) {
        n = label(s, dst, n, cap, foot, lay.foot, 0.78f, 0.82f, 0.88f, false);
    }
    n = filesGesture(s, b, lay, dst, n, cap);
    return n;
}

uint32_t filesGesture(UiState& s, const Browser& b, const Lay& lay, UiPrimitive* dst, uint32_t n, uint32_t cap) {
    if (b.confirm != 0) {
        n = boxRect(dst, n, cap, lay.yes, 0.55f, 0.24f, 0.22f, 1.0f, 4.0f, nullptr);
        n = label(s, dst, n, cap, "YES", lay.yes, 1.0f, 1.0f, 1.0f, true, false);
        n = boxRect(dst, n, cap, lay.no, 0.18f, 0.2f, 0.24f, 1.0f, 4.0f, nullptr);
        n = label(s, dst, n, cap, "NO", lay.no, 0.9f, 0.9f, 0.9f, true, false);
    }
    if (b.menu != 0) {
        for (int i = 0; i < lay.menuN; ++i) {
            n = boxRect(dst, n, cap, lay.menu[i], 0.12f, 0.13f, 0.16f, 0.98f, i == 0 ? 4.0f : 0.0f, nullptr);
            n = label(s, dst, n, cap, menuName(i), lay.menu[i], 0.93f, 0.94f, 0.96f, false, false);
        }
    }
    if (b.banding != 0) {
        const float left = b.bandX0 < b.dragX ? b.bandX0 : b.dragX;
        const float top = b.bandY0 < b.dragY ? b.bandY0 : b.dragY;
        Zone band{left, top, std::fabs(b.dragX - b.bandX0), std::fabs(b.dragY - b.bandY0)};
        n = boxRect(dst, n, cap, band, b.look.accent[0], b.look.accent[1], b.look.accent[2], 0.28f, 2.0f, nullptr);
    }
    if (b.dragging != 0 && b.drag >= 0 && b.drag < b.count) {
        Zone ghost{b.dragX + 12.0f, b.dragY + 12.0f, 160.0f, 24.0f};
        n = boxRect(dst, n, cap, ghost, b.look.accent[0], b.look.accent[1], b.look.accent[2], 0.9f, 4.0f, nullptr);
        n = label(s, dst, n, cap, b.items[b.drag].name, ghost, 1.0f, 1.0f, 1.0f, false);
    }
    return n;
}

bool filesDraw(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id != s.fileView || s.fileView == kUiNone) {
        return false;
    }
    if (cellsMounted(s, viewBook(s))) {
        return false;
    }
    n = filesEmit(s, dst, n, cap);
    return true;
}

bool filesOver(UiState& s, uint16_t, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (!shown(s) || s.files == nullptr || !cellsMounted(s, viewBook(s))) {
        return false;
    }
    const Browser& b = viewBook(s);
    Lay lay{};
    layout(s, b, lay);
    n = filesGesture(s, b, lay, dst, n, cap);
    return true;
}

void filesAttach(UiState& s) {
    s.plug.handle = filesHandle;
    s.plug.draw = filesDraw;
    s.plug.over = filesOver;
    s.plug.wantsText = filesWantsText;
    s.plug.shutdown = filesShutdown;
}

} // namespace burnhope
