#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace burnhope {

enum class FileKind : uint8_t { Folder = 0, Scene, Model, Texture, Material, Other };
constexpr uint32_t kFileKinds = 6;

enum class SortMode : uint8_t { Name = 0, Size, Type, Date };

// One shown file-grid tile: the path behind it and what a double click does with it.
struct BrowserItem {
    std::filesystem::path path;
    FileKind kind = FileKind::Other;
    uintmax_t size = 0;
    int64_t mtime = 0;
    uint8_t cell = 0; // 1 + thumbnail atlas cell once resolved; 0 = not yet
};

struct BrowserQuery {
    const char* filter = "";  // case-insensitive name substring
    uint8_t kindFilter = 0;   // 0 all, 1 textures, 2 materials, 3 scenes, 4 models
    SortMode sort = SortMode::Name;
    bool showHidden = false;
};

[[nodiscard]] FileKind fileKindOf(const std::filesystem::path& p);
// Localized type shown under a tile name: "Папка", "Изображение", "Материал", "3D Модель", "Сцена", "Файл".
[[nodiscard]] const char* fileKindCaption(FileKind kind);
// "597 Б", "1.5 КиБ", "3.2 МиБ".
void fileSizeText(char* out, size_t cap, uintmax_t bytes);
// Russian noun form for `n`: one / few / many ("1 папка", "2 папки", "5 папок").
[[nodiscard]] const char* pluralRu(uint32_t n, const char* one, const char* few, const char* many);
// Lists `dir` into `out` (cleared first), folders first, ordered by `query.sort`.
void browserScan(const std::filesystem::path& dir, const BrowserQuery& query, std::vector<BrowserItem>& out);
// True when `dir` holds at least one listed subfolder.
[[nodiscard]] bool dirHasFolders(const std::filesystem::path& dir);
[[nodiscard]] bool dirListed(const std::filesystem::path& p, bool showHidden = false);

} // namespace burnhope
