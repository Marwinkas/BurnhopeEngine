#include "app/BrowserModel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace burnhope {
namespace fs = std::filesystem;
namespace {

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool passesKind(uint8_t filter, FileKind kind) {
    if (kind == FileKind::Folder) {
        return true; // folders always stay so the tree can be walked
    }
    switch (filter) {
    case 1: return kind == FileKind::Texture;
    case 2: return kind == FileKind::Material;
    case 3: return kind == FileKind::Scene;
    case 4: return kind == FileKind::Model;
    default: return true;
    }
}

} // namespace

bool dirListed(const fs::path& p, bool showHidden) {
    const std::string name = p.filename().string();
    if (name.empty() || name == "build-core" || name == "cache" || name == ".trash") {
        return false;
    }
    return showHidden || name[0] != '.';
}

FileKind fileKindOf(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        return FileKind::Folder;
    }
    const std::string ext = lowered(p.extension().string());
    if (ext == ".bhop" || ext == ".bscene") return FileKind::Scene;
    if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") return FileKind::Model;
    if (ext == ".bhmat") return FileKind::Material;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".bmp") return FileKind::Texture;
    return FileKind::Other;
}

const char* fileKindCaption(FileKind kind) {
    switch (kind) {
    case FileKind::Folder: return "Папка";
    case FileKind::Scene: return "Сцена";
    case FileKind::Model: return "3D Модель";
    case FileKind::Texture: return "Изображение";
    case FileKind::Material: return "Материал";
    default: return "Файл";
    }
}

void fileSizeText(char* out, size_t cap, uintmax_t bytes) {
    constexpr const char* kUnit[] = {"Б", "КиБ", "МиБ", "ГиБ"};
    double value = static_cast<double>(bytes);
    uint32_t unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        std::snprintf(out, cap, "%llu %s", static_cast<unsigned long long>(bytes), kUnit[0]);
    } else {
        std::snprintf(out, cap, "%.1f %s", value, kUnit[unit]);
    }
}

const char* pluralRu(uint32_t n, const char* one, const char* few, const char* many) {
    const uint32_t tail = n % 100;
    if (tail >= 11 && tail <= 14) {
        return many;
    }
    switch (n % 10) {
    case 1: return one;
    case 2:
    case 3:
    case 4: return few;
    default: return many;
    }
}

bool dirHasFolders(const fs::path& dir) {
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec) && dirListed(entry.path())) {
            return true;
        }
    }
    return false;
}

void browserScan(const fs::path& dir, const BrowserQuery& query, std::vector<BrowserItem>& out) {
    out.clear();
    const std::string needle = lowered(query.filter);
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!dirListed(entry.path(), query.showHidden)) {
            continue;
        }
        if (!needle.empty() && lowered(entry.path().filename().string()).find(needle) == std::string::npos) {
            continue;
        }
        BrowserItem item;
        item.path = entry.path();
        item.kind = fileKindOf(item.path);
        if (!passesKind(query.kindFilter, item.kind)) {
            continue;
        }
        if (item.kind != FileKind::Folder) {
            item.size = entry.file_size(ec);
            if (ec) {
                item.size = 0;
            }
        }
        const fs::file_time_type stamp = entry.last_write_time(ec);
        item.mtime = ec ? 0 : static_cast<int64_t>(stamp.time_since_epoch().count());
        out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [&](const BrowserItem& l, const BrowserItem& r) {
        const bool lf = l.kind == FileKind::Folder;
        if (lf != (r.kind == FileKind::Folder)) {
            return lf;
        }
        switch (query.sort) {
        case SortMode::Size:
            if (l.size != r.size) return l.size > r.size;
            break;
        case SortMode::Type:
            if (l.kind != r.kind) return l.kind < r.kind;
            break;
        case SortMode::Date:
            if (l.mtime != r.mtime) return l.mtime > r.mtime;
            break;
        case SortMode::Name: break;
        }
        return lowered(l.path.filename().string()) < lowered(r.path.filename().string());
    });
}

} // namespace burnhope
