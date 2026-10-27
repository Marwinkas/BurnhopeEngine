#pragma once

#include <filesystem>

#include "app/BrowserController.hpp"

namespace burnhope {

// Shared by the Browser*.cpp coordinators: Controller (bind, navigation, menus), View (what the lists show) and Select
// (clicks, marks, marquee, open).
// Semantic node keys: the "bind" of the nodes in assets/gui/files.json and menus.json, never hierarchy paths.
inline constexpr const char* kBrowserTree = "browser.tree";
inline constexpr const char* kBrowserGrid = "browser.grid";
inline constexpr const char* kBrowserSearch = "browser.search";
inline constexpr const char* kBrowserFilter = "browser.filter";
inline constexpr const char* kBrowserPill = "browser.pill";

// View
[[nodiscard]] bool browserContains(const std::vector<std::filesystem::path>& list, const std::filesystem::path& p);
void browserShowTree(BrowserState& br);
void browserShowFiles(BrowserState& br);
void browserShowStatus(BrowserState& br);
void browserApplyTile(BrowserState& br);
void browserRestyleTiles(BrowserState& br);
// Makes `dir` the working directory (recording it in the history when `record`), unfolds its parents in the tree.
void browserEnter(BrowserState& br, const std::filesystem::path& dir, bool record);
// Number of path components of `dir` below the project root.
[[nodiscard]] uint32_t browserDepthOf(const BrowserState& b, const std::filesystem::path& dir);

// Select
void browserOnTree(void* user, uint32_t index, bool activate);
void browserOnFile(void* user, uint32_t index, bool activate);
void browserOnContext(void* user, uint32_t index);
void browserPollMarquee(BrowserState& br);
void browserOpen(BrowserState& br, const BrowserItem& item);

} // namespace burnhope
