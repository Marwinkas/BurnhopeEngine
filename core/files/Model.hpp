#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace burnhope {

struct FileItem {
    std::string name;
    uint8_t isDir = 0;
    uint8_t kind = 0;
    uint8_t selected = 0;
    uint8_t held = 0;
    uint64_t bytes = 0;
    uint64_t mtime = 0;
};

struct FileTreeNode {
    std::string rel;
    uint8_t depth = 0;
    uint8_t open = 0;
};

// Directory listing, history, bookmarks, and file operations. No widgets.
// Listings and the folder tree grow with the directory. There is no item ceiling.
struct FileModel {
    char cwd[512]{};
    char root[512]{};
    std::vector<std::string> hist;
    int histAt = 0;
    std::vector<FileItem> items;
    int total = 0;
    int scroll = 0;
    int anchor = 0;
    int confirm = 0;
    int mode = 0;
    int renameAt = -1;
    char search[40]{};
    std::string edit;
    int editAll = 0;
    std::vector<std::string> clip;
    int clipCut = 0;
    std::vector<std::string> fav;
    std::vector<FileTreeNode> tree;
    char filt[16]{};
    int kind = 0;
    uint8_t sortKey = 0;
    uint8_t groupOn = 0;
    uint8_t groupOpen = 0x1f;
    char status[96]{};
    uint8_t showHidden = 0;

    [[nodiscard]] int count() const { return static_cast<int>(items.size()); }
    [[nodiscard]] int treeCount() const { return static_cast<int>(tree.size()); }

    void load();
    void save() const;
    void reload();
    void rebuildTree();
    void go(const char* path);
    void setStatus(const char* text);
    bool underRoot(const char* path) const;
    [[nodiscard]] std::vector<std::string> selectedPaths() const;
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

// Layout preferences only. Paint comes from UiLook tokens.
struct FileLook {
    float icon = 84.0f;
    float tree = 188.0f;
    float details = 210.0f;
    uint8_t showTree = 1;
    uint8_t showDetails = 1;
    uint8_t showHidden = 0;
};

using FileOpenFn = void (*)(void* user, const char* path, int isDir);

} // namespace burnhope
