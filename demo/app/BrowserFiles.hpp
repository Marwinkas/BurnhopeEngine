#pragma once

#include <cstdint>
#include <filesystem>

namespace burnhope {

// Creates "New Folder", "New Material.bhmat", "New Scene.bscene" or "New File.txt" in `dir` under a unique name.
// Returns the created path, or an empty path when the filesystem refused.
enum class NewFile : uint8_t { Folder = 0, Material, Scene, Plain };
[[nodiscard]] std::filesystem::path browserCreate(const std::filesystem::path& dir, NewFile what);
// Copies `path` next to itself (a .gltf with its .bin, re-pointed) as "<stem> copy[ n]<ext>" (folders recursively). Empty path on failure.
[[nodiscard]] std::filesystem::path browserDuplicate(const std::filesystem::path& path);
// Moves `from` to `to` (a .gltf takes its same-stem .bin along). False when either target exists or the filesystem refused.
[[nodiscard]] bool browserMove(const std::filesystem::path& from, const std::filesystem::path& to);
// Copies the file `src` into `dir` under a unique name. Empty path on failure.
[[nodiscard]] std::filesystem::path browserImport(const std::filesystem::path& dir, const std::filesystem::path& src);
// Moves `path` into "<root>/.trash" (never deleted outright, so the action is reversible by hand). False on failure.
[[nodiscard]] bool browserTrash(const std::filesystem::path& root, const std::filesystem::path& path);

} // namespace burnhope
