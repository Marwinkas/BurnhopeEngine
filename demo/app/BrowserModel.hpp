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
    char name[120] = {}; // name shown on the tile / row (files without their extension), cut only by the byte cap
    FileKind kind = FileKind::Other;
    uintmax_t size = 0;
    int64_t mtime = 0;
    uint8_t cell = 0; // 1 + thumbnail atlas cell once resolved; 0 = not yet
    bool marked = false; // part of the (multi-)selection
};

struct BrowserQuery {
    const char* filter = "";  // case-insensitive name substring
    uint8_t kindFilter = 0;   // 0 all, 1 textures, 2 materials, 3 scenes, 4 models
    SortMode sort = SortMode::Name;
};

[[nodiscard]] FileKind fileKindOf(const std::filesystem::path& p);
// Type shown under a tile name: "Folder", "Texture", "Material", "Scene", "Model", "File".
[[nodiscard]] const char* fileKindCaption(FileKind kind);
// "597 B", "1.5 KB", "3.2 MB".
void fileSizeText(char* out, size_t cap, uintmax_t bytes);
// "27.10.2026 14:05" (UTC) from a filesystem clock tick count.
void fileDateText(char* out, size_t cap, int64_t mtime);
// Lists `dir` into `out` (cleared first), folders first, ordered by `query.sort`.
void browserScan(const std::filesystem::path& dir, const BrowserQuery& query, std::vector<BrowserItem>& out);
// True when `dir` holds at least one listed subfolder.
[[nodiscard]] bool dirHasFolders(const std::filesystem::path& dir);
[[nodiscard]] bool dirListed(const std::filesystem::path& p);
// The same-stem .bin buffer of a .gltf model (empty when `model` is not a .gltf or has none). The pair is one asset:
// the explorer lists only the model, and duplicate / trash / move carry the buffer along.
[[nodiscard]] std::filesystem::path modelCompanion(const std::filesystem::path& model);
// True for a .bin that belongs to a sibling .gltf (hidden in the explorer).
[[nodiscard]] bool isCompanion(const std::filesystem::path& p);

} // namespace burnhope
