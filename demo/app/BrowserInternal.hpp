#pragma once

#include <filesystem>

#include "app/BrowserController.hpp"

namespace burnhope {

// Shared by the Browser*.cpp coordinators: Controller (bind, navigation, menus), View (what the lists show) and Select
// (clicks, marks, marquee, open).
inline constexpr const char* kBrowserTree = "bottom::treeCol::tree";
inline constexpr const char* kBrowserGrid = "bottom::filesCol::grid";
inline constexpr const char* kBrowserBar = "bottom::filesCol::bar::";
inline constexpr const char* kBrowserPill = "bottom::filesCol::bar::pill::";

// View
[[nodiscard]] bool browserContains(const std::vector<std::filesystem::path>& list, const std::filesystem::path& p);
void browserShowTree(Engine& e);
void browserShowFiles(Engine& e);
void browserShowStatus(Engine& e);
void browserApplyTile(Engine& e);
void browserRestyleTiles(Engine& e);
// Makes `dir` the working directory (recording it in the history when `record`), unfolds its parents in the tree.
void browserEnter(Engine& e, const std::filesystem::path& dir, bool record);
// Number of path components of `dir` below the project root.
[[nodiscard]] uint32_t browserDepthOf(const BrowserState& b, const std::filesystem::path& dir);

// Select
void browserOnTree(void* user, uint32_t index, bool activate);
void browserOnFile(void* user, uint32_t index, bool activate);
void browserOnContext(void* user, uint32_t index);
void browserPollMarquee(Engine& e);
void browserOpen(Engine& e, const BrowserItem& item);

} // namespace burnhope
