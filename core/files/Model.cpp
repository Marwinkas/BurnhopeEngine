#include "files/Model.hpp"
#include "files/Format.hpp"
#include "platform/Desktop.hpp"
#include "platform/PlatformUtil.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace burnhope {
namespace fs = std::filesystem;

namespace {

bool searchHit(const FileModel& model, const std::string& name) {
    if (model.search[0] == '\0') {
        return true;
    }
    auto lower = [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    };
    std::string hay = name;
    std::string needle = model.search;
    for (char& c : hay) {
        c = lower(static_cast<unsigned char>(c));
    }
    for (char& c : needle) {
        c = lower(static_cast<unsigned char>(c));
    }
    return hay.find(needle) != std::string::npos;
}

std::vector<std::string> listDirs(const fs::path& abs, const FileModel& model, std::unordered_set<std::string>& seen) {
    std::vector<std::string> names;
    std::error_code canonEc;
    const fs::path canon = fs::weakly_canonical(abs, canonEc);
    const std::string key = canonEc ? abs.string() : canon.string();
    if (!seen.insert(key).second) {
        return names;
    }
    std::error_code treeEc;
    fs::directory_iterator treeIt(abs, fs::directory_options::skip_permission_denied, treeEc);
    if (treeEc) {
        return names;
    }
    for (; treeIt != fs::directory_iterator(); treeIt.increment(treeEc)) {
        if (treeEc) {
            break;
        }
        std::error_code ignored;
        if (treeIt->is_symlink(ignored) || ignored) {
            continue;
        }
        if (!treeIt->is_directory(ignored) || ignored) {
            continue;
        }
        const std::string filename = treeIt->path().filename().string();
        if (filename.empty() || (model.showHidden == 0 && filename[0] == '.')) {
            continue;
        }
        names.push_back(filename);
    }
    std::sort(names.begin(), names.end());
    return names;
}

struct TreeWalk {
    fs::path abs;
    std::string rel;
    int depth = 1;
    std::vector<std::string> names;
    size_t next = 0;
};

void packListed(FileModel& model, std::vector<FileItem> fresh) {
    if (model.groupOn == 0) {
        model.items = std::move(fresh);
        return;
    }
    const int n = static_cast<int>(fresh.size());
    model.items.clear();
    static const uint8_t keys[] = {1, 2, 3, 4, 0};
    static const char* titles[] = {"Folders", "Images", "Text", "Audio", "Other"};
    for (int g = 0; g < 5; ++g) {
        const uint8_t key = keys[g];
        int hits = 0;
        for (int i = 0; i < n; ++i) {
            const FileItem& item = fresh[static_cast<size_t>(i)];
            const bool dir = item.isDir != 0;
            if (key == 1 ? dir : (!dir && item.kind == key)) {
                ++hits;
            }
        }
        if (hits == 0) {
            continue;
        }
        const bool open = (model.groupOpen & static_cast<uint8_t>(1u << key)) != 0;
        char title[64]{};
        std::snprintf(title, sizeof(title), "%s  %s", open ? "v" : ">", titles[g]);
        FileItem head;
        head.isDir = 2;
        head.kind = key;
        head.name = title;
        model.items.push_back(std::move(head));
        if (!open) {
            continue;
        }
        for (int i = 0; i < n; ++i) {
            FileItem& item = fresh[static_cast<size_t>(i)];
            const bool dir = item.isDir != 0;
            if (key == 1 ? dir : (!dir && item.kind == key)) {
                model.items.push_back(std::move(item));
            }
        }
    }
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

void describeItem(const FileItem& item, char* dst, int cap) {
    char size[32]{};
    formatFileSize(item.bytes, size, 32);
    if (item.isDir != 0) {
        std::snprintf(dst, static_cast<size_t>(cap), "%s (%s)", item.name.c_str(), fileTypeName(item.isDir, item.kind));
        return;
    }
    std::snprintf(dst, static_cast<size_t>(cap), "%s (%s, %s)", item.name.c_str(), fileTypeName(item.isDir, item.kind), size);
}

std::string itemPath(const FileModel& model, const FileItem& item) {
    return (fs::path(model.cwd) / item.name).string();
}

} // namespace

void FileModel::setStatus(const char* text) {
    copyCap(status, 96, text);
}

bool FileModel::underRoot(const char* path) const {
    if (path == nullptr) {
        return false;
    }
    if (root[0] == '\0') {
        return true;
    }
    const size_t n = std::strlen(root);
    if (std::strncmp(path, root, n) != 0) {
        return false;
    }
    return path[n] == '\0' || path[n] == '/';
}

void FileModel::save() const {
    char path[512]{};
    if (!platformCachePath(path, sizeof(path), "burnhope-files.txt")) {
        return;
    }
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "dir %s\n", cwd);
    for (const std::string& pathText : fav) {
        std::fprintf(file, "fav %s\n", pathText.c_str());
    }
    std::fclose(file);
}

void FileModel::load() {
    char path[512]{};
    std::FILE* file = platformCachePath(path, sizeof(path), "burnhope-files.txt") ? std::fopen(path, "r") : nullptr;
    if (file != nullptr) {
        char line[4096]{};
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            size_t n = std::strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            if (std::strncmp(line, "dir ", 4) == 0 && cwd[0] == '\0') {
                copyCap(cwd, 512, line + 4);
            } else if (std::strncmp(line, "fav ", 4) == 0) {
                fav.emplace_back(line + 4);
            }
        }
        std::fclose(file);
    }
    if (cwd[0] == '\0') {
        copyCap(cwd, 512, ".");
    }
}

void FileModel::rebuildTree() {
    std::unordered_set<std::string> opened;
    uint8_t rootOpen = 1;
    for (const FileTreeNode& node : tree) {
        if (node.rel.empty()) {
            rootOpen = node.open;
        } else if (node.open != 0) {
            opened.insert(node.rel);
        }
    }
    tree.clear();
    const char* base = root[0] != '\0' ? root : cwd;
    if (base[0] == '\0') {
        return;
    }
    FileTreeNode rootNode;
    rootNode.depth = 0;
    rootNode.open = rootOpen;
    tree.push_back(std::move(rootNode));
    if (rootOpen == 0) {
        return;
    }
    std::unordered_set<std::string> seen;
    std::vector<TreeWalk> stack;
    TreeWalk first;
    first.abs = fs::path(base);
    first.depth = 1;
    first.names = listDirs(first.abs, *this, seen);
    stack.push_back(std::move(first));
    while (!stack.empty()) {
        if (stack.back().next >= stack.back().names.size()) {
            stack.pop_back();
            continue;
        }
        const std::string name = stack.back().names[stack.back().next++];
        const fs::path abs = stack.back().abs;
        const std::string rel = stack.back().rel;
        const int depth = stack.back().depth;
        FileTreeNode node;
        node.rel = rel.empty() ? name : rel + "/" + name;
        node.depth = static_cast<uint8_t>(depth > 255 ? 255 : depth);
        node.open = opened.contains(node.rel) ? 1 : 0;
        const bool open = node.open != 0;
        const fs::path child = abs / name;
        const std::string childRel = node.rel;
        tree.push_back(std::move(node));
        if (!open) {
            continue;
        }
        TreeWalk next;
        next.abs = child;
        next.rel = childRel;
        next.depth = depth + 1;
        next.names = listDirs(child, *this, seen);
        stack.push_back(std::move(next));
    }
}

void FileModel::reload() {
    std::vector<FileItem> fresh;
    std::error_code ec;
    fs::directory_iterator it(fs::path(cwd), fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            const fs::directory_entry& ent = *it;
            std::error_code ignored;
            const bool dir = ent.is_directory(ignored);
            const std::string filename = ent.path().filename().string();
            if (filename.empty() || (showHidden == 0 && filename[0] == '.')) {
                continue;
            }
            if (!dir && filt[0] != '\0') {
                const size_t fl = std::strlen(filt);
                if (filename.size() < fl || filename.compare(filename.size() - fl, fl, filt) != 0) {
                    continue;
                }
            }
            const int itemKind = fileKind(filename.c_str(), dir ? 1 : 0);
            if (kind == 1 && !dir) {
                continue;
            }
            if (kind == 2 && itemKind != 2) {
                continue;
            }
            if (kind == 3 && itemKind != 3) {
                continue;
            }
            if (kind == 4 && itemKind != 4) {
                continue;
            }
            if (!searchHit(*this, filename)) {
                continue;
            }
            FileItem item;
            item.name = filename;
            item.isDir = dir ? 1 : 0;
            item.kind = static_cast<uint8_t>(itemKind);
            if (!dir) {
                item.bytes = static_cast<uint64_t>(ent.file_size(ignored));
            }
            std::error_code timeEc;
            const auto stamp = ent.last_write_time(timeEc);
            if (!timeEc) {
                const auto ticks = stamp.time_since_epoch().count();
                item.mtime = ticks > 0 ? static_cast<uint64_t>(ticks) : 0;
            }
            fresh.push_back(std::move(item));
        }
    }
    const int key = sortKey;
    std::sort(fresh.begin(), fresh.end(), [key](const FileItem& a, const FileItem& c) {
        if (a.isDir != c.isDir) {
            return a.isDir > c.isDir;
        }
        if (key == 1 && a.bytes != c.bytes) {
            return a.bytes < c.bytes;
        }
        if (key == 2 && a.kind != c.kind) {
            return a.kind < c.kind;
        }
        if (key == 3 && a.mtime != c.mtime) {
            return a.mtime > c.mtime;
        }
        return a.name < c.name;
    });
    total = static_cast<int>(fresh.size());
    packListed(*this, std::move(fresh));
    if (scroll > count()) {
        scroll = 0;
    }
    rebuildTree();
}

void FileModel::go(const char* path) {
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(path != nullptr ? path : "."), ec);
    if (ec) {
        setStatus("cannot open");
        return;
    }
    const std::string text = canon.string();
    if (!underRoot(text.c_str())) {
        setStatus("outside root");
        return;
    }
    std::error_code dirEc;
    if (!fs::is_directory(canon, dirEc)) {
        setStatus("not a folder");
        return;
    }
    if (text.empty() || !underRoot(text.c_str())) {
        return;
    }
    if (!hist.empty() && histAt >= 0 && histAt < static_cast<int>(hist.size())
        && sameText(hist[static_cast<size_t>(histAt)].c_str(), text.c_str())) {
        copyCap(cwd, static_cast<int>(sizeof(cwd)), text.c_str());
        return;
    }
    if (histAt + 1 < static_cast<int>(hist.size())) {
        hist.resize(static_cast<size_t>(histAt + 1));
    }
    hist.push_back(text);
    histAt = static_cast<int>(hist.size()) - 1;
    copyCap(cwd, static_cast<int>(sizeof(cwd)), text.c_str());
    scroll = 0;
    anchor = 0;
    confirm = 0;
    renameAt = -1;
    if (mode == 2) {
        mode = 0;
    }
    reload();
    save();
}

std::vector<std::string> FileModel::selectedPaths() const {
    std::vector<std::string> out;
    for (const FileItem& item : items) {
        if (item.selected == 0 || item.isDir == 2) {
            continue;
        }
        out.push_back(itemPath(*this, item));
    }
    return out;
}

int FileModel::selectedCount() const {
    int n = 0;
    for (const FileItem& item : items) {
        if (item.selected != 0 && item.isDir != 2) {
            ++n;
        }
    }
    return n;
}

void FileModel::selectOnly(int index) {
    for (int i = 0; i < count(); ++i) {
        items[static_cast<size_t>(i)].selected = i == index ? 1 : 0;
    }
    anchor = index;
}

void FileModel::toggleSelect(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    FileItem& item = items[static_cast<size_t>(index)];
    item.selected = item.selected == 0 ? 1 : 0;
    anchor = index;
}

void FileModel::selectRange(int from, int to) {
    if (from > to) {
        const int swap = from;
        from = to;
        to = swap;
    }
    if (from < 0) {
        from = 0;
    }
    if (to >= count()) {
        to = count() - 1;
    }
    clearSelection();
    for (int i = from; i <= to && i < count(); ++i) {
        FileItem& item = items[static_cast<size_t>(i)];
        if (item.isDir == 2) {
            continue;
        }
        item.selected = 1;
    }
}

void FileModel::selectAll() {
    for (FileItem& item : items) {
        if (item.isDir == 2) {
            continue;
        }
        item.selected = 1;
    }
}

void FileModel::clearSelection() {
    for (FileItem& item : items) {
        item.selected = 0;
    }
}

bool FileModel::canBack() const { return histAt > 0; }

bool FileModel::canForward() const { return histAt + 1 < static_cast<int>(hist.size()); }

void FileModel::goBack() {
    if (!canBack()) {
        return;
    }
    if (!underRoot(hist[static_cast<size_t>(histAt - 1)].c_str())) {
        return;
    }
    --histAt;
    copyCap(cwd, static_cast<int>(sizeof(cwd)), hist[static_cast<size_t>(histAt)].c_str());
    renameAt = -1;
    reload();
}

void FileModel::goForward() {
    if (!canForward()) {
        return;
    }
    if (!underRoot(hist[static_cast<size_t>(histAt + 1)].c_str())) {
        return;
    }
    ++histAt;
    copyCap(cwd, static_cast<int>(sizeof(cwd)), hist[static_cast<size_t>(histAt)].c_str());
    renameAt = -1;
    reload();
}

void FileModel::startRename(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    renameAt = index;
    mode = 2;
    edit = items[static_cast<size_t>(index)].name;
}

void FileModel::cancelRename() {
    renameAt = -1;
    mode = 0;
    edit.clear();
}

bool FileModel::commitRename(const char* newName) {
    if (newName == nullptr || newName[0] == '\0') {
        cancelRename();
        return false;
    }
    edit = newName;
    commitRename();
    return true;
}

void FileModel::paste() {
    int pasted = 0;
    for (const std::string& srcText : clip) {
        std::error_code ec;
        const fs::path src(srcText);
        if (!fs::exists(src, ec)) {
            continue;
        }
        char stem[256]{};
        char ext[32]{};
        splitStem(src.filename().string().c_str(), stem, 256, ext, 32);
        char dst[4096]{};
        uniquePath(dst, static_cast<int>(sizeof(dst)), cwd, stem, ext);
        if (clipCut != 0) {
            fs::rename(src, fs::path(dst), ec);
            if (ec) {
                continue;
            }
        } else if (!copyOne(srcText.c_str(), dst)) {
            continue;
        }
        ++pasted;
    }
    if (clipCut != 0) {
        clip.clear();
        clipCut = 0;
    }
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "pasted %d", pasted);
    setStatus(buf);
}

void FileModel::removeSelected() {
    int removed = 0;
    for (const FileItem& item : items) {
        if (item.selected == 0 || item.isDir == 2) {
            continue;
        }
        std::error_code ec;
        fs::remove_all(fs::path(cwd) / item.name, ec);
        if (!ec) {
            ++removed;
        }
    }
    confirm = 0;
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "deleted %d", removed);
    setStatus(buf);
}

void FileModel::duplicate() {
    const std::vector<std::string> paths = selectedPaths();
    if (paths.empty()) {
        setStatus("nothing selected");
        return;
    }
    int made = 0;
    for (const std::string& srcText : paths) {
        const fs::path src(srcText);
        char stem[256]{};
        char ext[32]{};
        splitStem(src.filename().string().c_str(), stem, 256, ext, 32);
        char dst[4096]{};
        uniquePath(dst, static_cast<int>(sizeof(dst)), cwd, stem, ext);
        if (copyOne(srcText.c_str(), dst)) {
            ++made;
        }
    }
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "duplicated %d", made);
    setStatus(buf);
}

void FileModel::commitRename() {
    if (renameAt < 0 || renameAt >= count() || edit.empty()) {
        mode = 0;
        renameAt = -1;
        return;
    }
    const fs::path src = fs::path(cwd) / items[static_cast<size_t>(renameAt)].name;
    const fs::path dst = fs::path(cwd) / edit;
    std::error_code ec;
    if (src != dst) {
        fs::rename(src, dst, ec);
    }
    mode = 0;
    renameAt = -1;
    editAll = 0;
    reload();
    setStatus(ec ? "rename failed" : "renamed");
}

void FileModel::addFavorite(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    for (const std::string& have : fav) {
        if (sameText(have.c_str(), path)) {
            return;
        }
    }
    fav.emplace_back(path);
    save();
    setStatus("favorite");
}

void FileModel::moveInto(const char* destDir) {
    if (destDir == nullptr) {
        return;
    }
    const std::vector<std::string> moving = selectedPaths();
    if (moving.empty()) {
        return;
    }
    int moved = 0;
    for (const std::string& srcText : moving) {
        const fs::path src(srcText);
        if (src.parent_path() == fs::path(destDir)) {
            continue;
        }
        std::error_code ec;
        fs::rename(src, fs::path(destDir) / src.filename(), ec);
        if (!ec) {
            ++moved;
        }
    }
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "moved %d", moved);
    setStatus(buf);
}

void FileModel::copySelection(int cut) {
    clip = selectedPaths();
    clipCut = cut;
    setStatus(cut != 0 ? "cut" : "copied");
}

void FileModel::reveal() {
    std::vector<std::string> paths = selectedPaths();
    if (paths.empty()) {
        paths.emplace_back(cwd);
    }
    std::vector<const char*> list;
    list.reserve(paths.size());
    for (const std::string& path : paths) {
        list.push_back(path.c_str());
    }
    desktopReveal(list.data(), static_cast<int>(list.size()));
    setStatus("shown in folder");
}

void FileModel::touchFile(const char* name) {
    const fs::path path = fs::path(cwd) / (name != nullptr ? name : "");
    std::FILE* file = std::fopen(path.c_str(), "w");
    if (file != nullptr) {
        std::fclose(file);
    }
    reload();
}

bool FileModel::createFolder(char* created, int cap) {
    char dst[4096]{};
    uniquePath(dst, static_cast<int>(sizeof(dst)), cwd, "New Folder", "");
    std::error_code ec;
    fs::create_directory(fs::path(dst), ec);
    if (ec) {
        setStatus("create failed");
        return false;
    }
    reload();
    const char* name = std::strrchr(dst, '/');
    name = name != nullptr ? name + 1 : dst;
    copyCap(created, cap, name);
    return true;
}

void FileModel::formatFoot(char* dst, int cap) const {
    dst[0] = '\0';
    int files = 0;
    int dirs = 0;
    uint64_t pickedBytes = 0;
    for (const FileItem& item : items) {
        if (item.selected == 0) {
            continue;
        }
        if (item.isDir != 0) {
            ++dirs;
        } else {
            ++files;
            pickedBytes += item.bytes;
        }
    }
    if (files + dirs == 1) {
        for (const FileItem& item : items) {
            if (item.selected != 0) {
                describeItem(item, dst, cap);
                return;
            }
        }
    }
    if (files > 1 && dirs == 0) {
        char size[32]{};
        formatFileSize(pickedBytes, size, 32);
        std::snprintf(dst, static_cast<size_t>(cap), "Выбраны %d %s (%s)", files, ruNoun(files, "файл", "файла", "файлов"), size);
        return;
    }
    if (dirs > 1 && files == 0) {
        std::snprintf(dst, static_cast<size_t>(cap), "Выбраны %d %s", dirs, ruNoun(dirs, "папка", "папки", "папок"));
        return;
    }
    if (files + dirs > 1) {
        char size[32]{};
        formatFileSize(pickedBytes, size, 32);
        std::snprintf(dst, static_cast<size_t>(cap), "Выбраны %d %s (%s)", files + dirs, ruNoun(files + dirs, "файл", "файла", "файлов"), size);
        return;
    }
    int folderN = 0;
    int fileN = 0;
    uint64_t bytes = 0;
    for (const FileItem& item : items) {
        if (item.isDir != 0) {
            ++folderN;
        } else {
            ++fileN;
            bytes += item.bytes;
        }
    }
    char size[32]{};
    formatFileSize(bytes, size, 32);
    std::snprintf(
        dst, static_cast<size_t>(cap), "%d %s, %d %s (%s)",
        folderN, ruNoun(folderN, "папка", "папки", "папок"),
        fileN, ruNoun(fileN, "файл", "файла", "файлов"), size);
}

} // namespace burnhope
