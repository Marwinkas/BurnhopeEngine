#pragma once

#include <cstdint>

namespace burnhope {

constexpr int kFileCap = 256;
constexpr int kClipCap = 12;
constexpr int kFavCap = 8;
constexpr int kHistCap = 16;
constexpr int kTreeCap = 64;

struct FileItem {
    char name[64]{};
    uint8_t isDir = 0;
    uint8_t kind = 0;
    uint8_t selected = 0;
    uint64_t bytes = 0;
    uint64_t mtime = 0;
};

// Directory listing, history, bookmarks, and file operations. No widgets.
struct FileModel {
    char cwd[512]{};
    char root[512]{};
    char hist[kHistCap][512]{};
    int histAt = 0;
    int histN = 0;
    FileItem items[kFileCap]{};
    int count = 0;
    int total = 0;
    int scroll = 0;
    int anchor = 0;
    int confirm = 0;
    int mode = 0;
    int renameAt = -1;
    char search[40]{};
    char edit[64]{};
    int editAll = 0;
    char clip[kClipCap][512]{};
    int clipN = 0;
    int clipCut = 0;
    char fav[kFavCap][512]{};
    int favN = 0;
    char tree[kTreeCap][64]{};
    uint8_t treeDepth[kTreeCap]{};
    uint8_t treeOpen[kTreeCap]{};
    int treeN = 0;
    char filt[16]{};
    int kind = 0;
    uint8_t sortKey = 0;
    uint8_t groupOn = 0;
    uint8_t groupOpen = 0x1f;
    char status[96]{};
    uint8_t showHidden = 0;

    void load();
    void save() const;
    void reload();
    void rebuildTree();
    void go(const char* path);
    void setStatus(const char* text);
    bool underRoot(const char* path) const;
    void selectedPaths(char out[][512], int cap, int& n) const;
    int selectedCount() const;
    void selectOnly(int index);
    void toggleSelect(int index);
    void selectRange(int from, int to);
    void selectAll();
    void clearSelection();
    [[nodiscard]] bool canBack() const;
    [[nodiscard]] bool canForward() const;
    void goBack();
    void goForward();
    void startRename(int index);
    void cancelRename();
    bool commitRename(const char* newName);
    void paste();
    void removeSelected();
    void duplicate();
    void commitRename();
    void addFavorite(const char* path);
    void moveInto(const char* destDir);
    void copySelection(int cut);
    void reveal();
    void touchFile(const char* name);
    bool createFolder(char* created, int cap);
    void formatFoot(char* dst, int cap) const;
};

struct FileLook {
    float icon = 84.0f;
    float tree = 188.0f;
    float details = 210.0f;
    uint8_t showTree = 1;
    uint8_t showDetails = 1;
    uint8_t showHidden = 0;
    float side[3]{0.11f, 0.122f, 0.145f};
    float grid[3]{0.07f, 0.075f, 0.09f};
    float card[3]{0.15f, 0.16f, 0.19f};
    float accent[3]{0.28f, 0.48f, 0.92f};
    float folder[3]{0.91f, 0.72f, 0.29f};
};

using FileOpenFn = void (*)(void* user, const char* path, int isDir);

} // namespace burnhope
