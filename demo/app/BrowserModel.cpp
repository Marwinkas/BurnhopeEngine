#include "app/BrowserModel.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

namespace burnhope {
namespace fs = std::filesystem;
namespace {

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalNoCase(std::string_view l, std::string_view r) {
    return l.size() == r.size() && std::equal(l.begin(), l.end(), r.begin(), [](char a, char b) { return lower(a) == lower(b); });
}

bool containsNoCase(std::string_view hay, std::string_view needle) {
    return needle.empty() || std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) { return lower(a) == lower(b); }) != hay.end();
}

bool lessNoCase(std::string_view l, std::string_view r) {
    return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end(), [](char a, char b) { return lower(a) < lower(b); });
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

fs::path modelCompanion(const fs::path& model) {
    std::error_code ec;
    if (!equalNoCase(model.extension().string(), ".gltf")) {
        return {};
    }
    fs::path bin = model;
    bin.replace_extension(".bin");
    return fs::is_regular_file(bin, ec) ? bin : fs::path{};
}

bool isCompanion(const fs::path& p) {
    std::error_code ec;
    if (!equalNoCase(p.extension().string(), ".bin")) {
        return false;
    }
    fs::path model = p;
    model.replace_extension(".gltf");
    return fs::is_regular_file(model, ec);
}

bool dirListed(const fs::path& p) {
    const std::string name = p.filename().string();
    if (name.empty() || name == "build-core" || name == "cache" || name == ".trash") {
        return false;
    }
    return name[0] != '.';
}

FileKind fileKindOf(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        return FileKind::Folder;
    }
    const std::string ext = p.extension().string();
    const auto is = [&](std::initializer_list<std::string_view> names) {
        return std::any_of(names.begin(), names.end(), [&](std::string_view n) { return equalNoCase(ext, n); });
    };
    if (is({".bhop", ".bscene"})) return FileKind::Scene;
    if (is({".gltf", ".glb", ".fbx", ".obj"})) return FileKind::Model;
    if (is({".bhmat"})) return FileKind::Material;
    if (is({".png", ".jpg", ".jpeg", ".dds", ".tga", ".bmp"})) return FileKind::Texture;
    return FileKind::Other;
}

const char* fileKindCaption(FileKind kind) {
    switch (kind) {
    case FileKind::Folder: return "Folder";
    case FileKind::Scene: return "Scene";
    case FileKind::Model: return "Model";
    case FileKind::Texture: return "Texture";
    case FileKind::Material: return "Material";
    default: return "File";
    }
}

void fileSizeText(char* out, size_t cap, uintmax_t bytes) {
    constexpr const char* kUnit[] = {"B", "KB", "MB", "GB"};
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

void fileDateText(char* out, size_t cap, int64_t mtime) {
    namespace ch = std::chrono;
    const fs::file_time_type stamp{fs::file_time_type::duration(mtime)};
    const ch::sys_seconds sec = ch::floor<ch::seconds>(ch::clock_cast<ch::system_clock>(stamp));
    const ch::sys_days day = ch::floor<ch::days>(sec);
    const ch::year_month_day ymd{day};
    const ch::hh_mm_ss<ch::seconds> tod{sec - day};
    std::snprintf(out, cap, "%02u.%02u.%04d %02d:%02d", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()),
                  static_cast<int>(ymd.year()), static_cast<int>(tod.hours().count()), static_cast<int>(tod.minutes().count()));
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
    const std::string_view needle(query.filter);
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!dirListed(entry.path()) || isCompanion(entry.path())) {
            continue;
        }
        if (!needle.empty() && !containsNoCase(entry.path().filename().native(), needle)) {
            continue;
        }
        BrowserItem item;
        item.path = entry.path();
        item.kind = fileKindOf(item.path);
        const std::string shown = item.kind == FileKind::Folder ? item.path.filename().string() : item.path.stem().string();
        std::snprintf(item.name, sizeof(item.name), "%s", shown.c_str());
        if (!passesKind(query.kindFilter, item.kind)) {
            continue;
        }
        if (item.kind != FileKind::Folder) {
            item.size = entry.file_size(ec);
            if (ec) {
                item.size = 0;
            }
            if (const fs::path buffer = modelCompanion(item.path); !buffer.empty()) {
                item.size += fs::file_size(buffer, ec); // the pair is listed as one asset
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
        return lessNoCase(l.path.filename().native(), r.path.filename().native());
    });
}

} // namespace burnhope
