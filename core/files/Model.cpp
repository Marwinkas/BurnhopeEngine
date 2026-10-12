#include "files/Model.hpp"
#include "files/Format.hpp"
#include "platform/Desktop.hpp"
#include "platform/PlatformUtil.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace burnhope {
namespace fs = std::filesystem;

namespace {

bool searchHit(const FileModel& model, const char* name) {
    if (model.search[0] == '\0') {
        return true;
    }
    char a[64]{};
    char c[40]{};
    copyCap(a, 64, name);
    copyCap(c, 40, model.search);
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

bool treeWasOpen(const char prev[][64], const uint8_t* prevOpen, int prevN, const char* rel) {
    for (int i = 0; i < prevN; ++i) {
        if (prevOpen[i] != 0 && std::strcmp(prev[i], rel) == 0) {
            return true;
        }
    }
    return false;
}

void pushTreeDirs(FileModel& model, const char* abs, const char* rel, int depth, const char prev[][64], const uint8_t* prevOpen, int prevN) {
    if (depth > 3 || model.treeN >= kTreeCap) {
        return;
    }
    char names[kTreeCap][64]{};
    int nameN = 0;
    std::error_code treeEc;
    fs::directory_iterator treeIt(fs::path(abs), fs::directory_options::skip_permission_denied, treeEc);
    if (treeEc) {
        return;
    }
    for (; treeIt != fs::directory_iterator() && nameN < kTreeCap; treeIt.increment(treeEc)) {
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
        if (model.showHidden == 0 && filename[0] == '.') {
            continue;
        }
        copyCap(names[nameN], 64, filename.c_str());
        ++nameN;
    }
    for (int i = 1; i < nameN; ++i) {
        char key[64]{};
        copyCap(key, 64, names[i]);
        int j = i;
        while (j > 0 && std::strcmp(names[j - 1], key) > 0) {
            copyCap(names[j], 64, names[j - 1]);
            --j;
        }
        copyCap(names[j], 64, key);
    }
    for (int i = 0; i < nameN && model.treeN < kTreeCap; ++i) {
        char path[64]{};
        if (rel[0] == '\0') {
            copyCap(path, 64, names[i]);
        } else {
            std::snprintf(path, sizeof(path), "%s/%s", rel, names[i]);
        }
        const int slot = model.treeN;
        copyCap(model.tree[slot], 64, path);
        model.treeDepth[slot] = static_cast<uint8_t>(depth);
        model.treeOpen[slot] = treeWasOpen(prev, prevOpen, prevN, path) ? 1 : 0;
        ++model.treeN;
        if (model.treeOpen[slot] != 0) {
            char child[512]{};
            joinPath(child, 512, abs, names[i]);
            pushTreeDirs(model, child, path, depth + 1, prev, prevOpen, prevN);
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
        std::snprintf(dst, static_cast<size_t>(cap), "%s (%s)", item.name, fileTypeName(item.isDir, item.kind));
        return;
    }
    std::snprintf(dst, static_cast<size_t>(cap), "%s (%s, %s)", item.name, fileTypeName(item.isDir, item.kind), size);
}

} // namespace

void FileModel::setStatus(const char* text) {
    copyCap(status, 96, text);
}

bool FileModel::underRoot(const char* path) const {
    if (root[0] == '\0' || path == nullptr) {
        return false;
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
    for (int i = 0; i < favN; ++i) {
        std::fprintf(file, "fav %s\n", fav[i]);
    }
    std::fclose(file);
}

void FileModel::load() {
    char path[512]{};
    std::FILE* file = platformCachePath(path, sizeof(path), "burnhope-files.txt") ? std::fopen(path, "r") : nullptr;
    if (file != nullptr) {
        char line[640]{};
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            size_t n = std::strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            if (std::strncmp(line, "dir ", 4) == 0 && cwd[0] == '\0') {
                copyCap(cwd, 512, line + 4);
            } else if (std::strncmp(line, "fav ", 4) == 0 && favN < kFavCap) {
                copyCap(fav[favN], 512, line + 4);
                ++favN;
            }
        }
        std::fclose(file);
    }
    if (cwd[0] == '\0') {
        copyCap(cwd, 512, ".");
    }
}

void FileModel::rebuildTree() {
    char prev[kTreeCap][64]{};
    uint8_t prevOpen[kTreeCap]{};
    const int prevN = treeN;
    for (int i = 0; i < prevN && i < kTreeCap; ++i) {
        copyCap(prev[i], 64, tree[i]);
        prevOpen[i] = treeOpen[i];
    }
    treeN = 0;
    const char* base = root[0] != '\0' ? root : cwd;
    pushTreeDirs(*this, base, "", 0, prev, prevOpen, prevN);
}

void FileModel::reload() {
    if (root[0] == '\0') {
        count = 0;
        total = 0;
        treeN = 0;
        status[0] = '\0';
        return;
    }
    FileItem fresh[kFileCap]{};
    int n = 0;
    int listed = 0;
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
            if (filename.empty() || filename.size() >= 63) {
                continue;
            }
            if (showHidden == 0 && filename[0] == '.') {
                continue;
            }
            if (!dir && filt[0] != '\0') {
                const size_t fn = filename.size();
                const size_t fl = std::strlen(filt);
                if (fn < fl || std::strcmp(filename.c_str() + (fn - fl), filt) != 0) {
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
            if (!searchHit(*this, filename.c_str())) {
                continue;
            }
            ++listed;
            if (n >= kFileCap) {
                continue;
            }
            copyCap(fresh[n].name, 64, filename.c_str());
            fresh[n].isDir = dir ? 1 : 0;
            fresh[n].kind = static_cast<uint8_t>(itemKind);
            if (!dir) {
                fresh[n].bytes = static_cast<uint64_t>(ent.file_size(ignored));
            }
            ++n;
        }
    }
    std::sort(fresh, fresh + n, [](const FileItem& a, const FileItem& c) {
        if (a.isDir != c.isDir) {
            return a.isDir > c.isDir;
        }
        return std::strcmp(a.name, c.name) < 0;
    });
    count = n;
    total = listed;
    for (int i = 0; i < n; ++i) {
        items[i] = fresh[i];
    }
    if (scroll > count) {
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
    if (histN > 0 && sameText(hist[histAt], text.c_str())) {
        copyCap(cwd, 512, text.c_str());
        return;
    }
    if (histAt + 1 < histN) {
        histN = histAt + 1;
    }
    if (histN >= kHistCap) {
        for (int i = 1; i < kHistCap; ++i) {
            copyCap(hist[i - 1], 512, hist[i]);
        }
        histN = kHistCap - 1;
        histAt = histN - 1;
    }
    copyCap(hist[histN], 512, text.c_str());
    histAt = histN;
    ++histN;
    copyCap(cwd, 512, text.c_str());
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

void FileModel::selectedPaths(char out[][512], int cap, int& n) const {
    n = 0;
    for (int i = 0; i < count && n < cap; ++i) {
        if (items[i].selected == 0 || items[i].isDir == 2) {
            continue;
        }
        joinPath(out[n], 512, cwd, items[i].name);
        ++n;
    }
}

int FileModel::selectedCount() const {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i].selected != 0) {
            ++n;
        }
    }
    return n;
}

void FileModel::selectOnly(int index) {
    for (int i = 0; i < count; ++i) {
        items[i].selected = i == index ? 1 : 0;
    }
    anchor = index;
}

void FileModel::toggleSelect(int index) {
    if (index < 0 || index >= count) {
        return;
    }
    items[index].selected = items[index].selected == 0 ? 1 : 0;
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
    if (to >= count) {
        to = count - 1;
    }
    clearSelection();
    for (int i = from; i <= to && i < count; ++i) {
        items[i].selected = 1;
    }
}

void FileModel::selectAll() {
    for (int i = 0; i < count; ++i) {
        items[i].selected = 1;
    }
}

void FileModel::clearSelection() {
    for (int i = 0; i < count; ++i) {
        items[i].selected = 0;
    }
}

bool FileModel::canBack() const { return histAt > 0; }

bool FileModel::canForward() const { return histAt + 1 < histN; }

void FileModel::goBack() {
    if (!canBack()) {
        return;
    }
    --histAt;
    copyCap(cwd, 512, hist[histAt]);
    renameAt = -1;
    reload();
}

void FileModel::goForward() {
    if (!canForward()) {
        return;
    }
    ++histAt;
    copyCap(cwd, 512, hist[histAt]);
    renameAt = -1;
    reload();
}

void FileModel::startRename(int index) {
    if (index < 0 || index >= count) {
        return;
    }
    renameAt = index;
    mode = 2;
    copyCap(edit, 64, items[index].name);
}

void FileModel::cancelRename() {
    renameAt = -1;
    mode = 0;
    edit[0] = '\0';
}

bool FileModel::commitRename(const char* newName) {
    if (newName == nullptr || newName[0] == '\0') {
        cancelRename();
        return false;
    }
    copyCap(edit, 64, newName);
    commitRename();
    return true;
}

void FileModel::paste() {
    int pasted = 0;
    for (int i = 0; i < clipN; ++i) {
        std::error_code ec;
        if (!fs::exists(fs::path(clip[i]), ec)) {
            continue;
        }
        const fs::path src(clip[i]);
        char stem[64]{};
        char ext[16]{};
        splitStem(src.filename().string().c_str(), stem, 64, ext, 16);
        char dst[512]{};
        uniquePath(dst, 512, cwd, stem, ext);
        if (clipCut != 0) {
            fs::rename(src, fs::path(dst), ec);
            if (ec) {
                continue;
            }
        } else if (!copyOne(clip[i], dst)) {
            continue;
        }
        ++pasted;
    }
    if (clipCut != 0) {
        clipN = 0;
        clipCut = 0;
    }
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "pasted %d", pasted);
    setStatus(buf);
}

void FileModel::removeSelected() {
    int removed = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i].selected == 0) {
            continue;
        }
        char path[512]{};
        joinPath(path, 512, cwd, items[i].name);
        std::error_code ec;
        fs::remove_all(fs::path(path), ec);
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
    char paths[kClipCap][512]{};
    int n = 0;
    selectedPaths(paths, kClipCap, n);
    if (n == 0) {
        setStatus("nothing selected");
        return;
    }
    int made = 0;
    for (int i = 0; i < n; ++i) {
        const fs::path src(paths[i]);
        char stem[64]{};
        char ext[16]{};
        splitStem(src.filename().string().c_str(), stem, 64, ext, 16);
        char dst[512]{};
        uniquePath(dst, 512, cwd, stem, ext);
        if (copyOne(paths[i], dst)) {
            ++made;
        }
    }
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "duplicated %d", made);
    setStatus(buf);
}

void FileModel::commitRename() {
    if (renameAt < 0 || renameAt >= count || edit[0] == '\0') {
        mode = 0;
        renameAt = -1;
        return;
    }
    char src[512]{};
    char dst[512]{};
    joinPath(src, 512, cwd, items[renameAt].name);
    joinPath(dst, 512, cwd, edit);
    std::error_code ec;
    if (!sameText(src, dst)) {
        fs::rename(fs::path(src), fs::path(dst), ec);
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
    for (int i = 0; i < favN; ++i) {
        if (sameText(fav[i], path)) {
            return;
        }
    }
    if (favN >= kFavCap) {
        return;
    }
    copyCap(fav[favN++], 512, path);
    save();
    setStatus("favorite");
}

void FileModel::moveInto(const char* destDir) {
    if (destDir == nullptr) {
        return;
    }
    char moving[kClipCap][512]{};
    int n = 0;
    selectedPaths(moving, kClipCap, n);
    if (n == 0) {
        return;
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
    reload();
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "moved %d", moved);
    setStatus(buf);
}

void FileModel::copySelection(int cut) {
    clipN = 0;
    selectedPaths(clip, kClipCap, clipN);
    clipCut = cut;
    setStatus(cut != 0 ? "cut" : "copied");
}

void FileModel::reveal() {
    char paths[8][512]{};
    int n = 0;
    selectedPaths(paths, 8, n);
    if (n == 0) {
        copyCap(paths[0], 512, cwd);
        n = 1;
    }
    const char* list[8]{};
    for (int i = 0; i < n; ++i) {
        list[i] = paths[i];
    }
    desktopReveal(list, n);
    setStatus("shown in folder");
}

void FileModel::touchFile(const char* name) {
    char path[512]{};
    joinPath(path, 512, cwd, name);
    std::FILE* file = std::fopen(path, "w");
    if (file != nullptr) {
        std::fclose(file);
    }
    reload();
}

bool FileModel::createFolder(char* created, int cap) {
    char dst[512]{};
    uniquePath(dst, 512, cwd, "New Folder", "");
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
    if (root[0] == '\0') {
        return;
    }
    int files = 0;
    int dirs = 0;
    uint64_t pickedBytes = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i].selected == 0) {
            continue;
        }
        if (items[i].isDir != 0) {
            ++dirs;
        } else {
            ++files;
            pickedBytes += items[i].bytes;
        }
    }
    if (files + dirs == 1) {
        for (int i = 0; i < count; ++i) {
            if (items[i].selected != 0) {
                describeItem(items[i], dst, cap);
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
    for (int i = 0; i < count; ++i) {
        if (items[i].isDir != 0) {
            ++folderN;
        } else {
            ++fileN;
            bytes += items[i].bytes;
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
