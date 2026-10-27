#include "app/BrowserDrag.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <spdlog/spdlog.h>

#include "app/BrowserController.hpp"
#include "app/BrowserFiles.hpp"
#include "app/BrowserThumbs.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Input.hpp"
#include "render/SceneFrame.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kGhost = "dragGhost";
constexpr const char* kDropBox = "dropBox";
constexpr const char* kInspector = "inspector.panel";

// GuiDropHit::kind values of this application.
enum DropKind : uint32_t { kDropNone, kDropGridFolder, kDropTreeFolder, kDropInspector, kDropViewport };

bool inside(const BrowserState& br, const char* list, float px, float py) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    return guiHostBox(br.gui, list, x, y, w, h) && px >= x && px < x + w && py >= y && py < y + h && guiHostPointerWithin(br.gui, list);
}

uint32_t rowAt(const BrowserState& br, const char* list, float px, float py) {
    uint32_t hit = kGuiNoSelection;
    return inside(br, list, px, py) && guiHostListHits(br.gui, list, px, py, px + 1.0f, py + 1.0f, &hit, 1) == 1 ? hit : kGuiNoSelection;
}

// Tooltip of a tile (shown by the core after its hover delay): name, type, size and modification time.
bool tipText(void* user, uint32_t index, char* out, uint32_t cap) {
    const BrowserState& b = *static_cast<BrowserState*>(user);
    if (index >= b.files.size()) {
        return false;
    }
    if (out == nullptr) {
        return true;
    }
    const BrowserItem& item = b.files[index];
    char size[32];
    char date[32];
    fileSizeText(size, sizeof(size), item.size);
    fileDateText(date, sizeof(date), item.mtime);
    const std::string name = item.path.filename().string();
    if (item.kind == FileKind::Folder) {
        std::snprintf(out, cap, "%s\nType: %s\nModified: %s", name.c_str(), fileKindCaption(item.kind), date);
    } else {
        std::snprintf(out, cap, "%s\nType: %s\nSize: %s\nModified: %s", name.c_str(), fileKindCaption(item.kind), size, date);
    }
    return true;
}

bool carried(const std::vector<fs::path>& payload, const fs::path& folder) {
    return std::any_of(payload.begin(), payload.end(), [&](const fs::path& path) {
        const fs::path rel = folder.lexically_relative(path);
        return !rel.empty() && *rel.begin() != ".."; // the folder is a carried item or lies inside one
    });
}

void moveInto(BrowserState& br, const fs::path& folder) {
    uint32_t moved = 0;
    for (const fs::path& from : br.pointer.payload) {
        const fs::path to = folder / from.filename();
        std::error_code ec;
        if (from.parent_path() == folder || fs::exists(to, ec)) {
            continue;
        }
        if (!browserMove(from, to)) {
            spdlog::warn("browser: cannot move {} to {}", from.string(), folder.string());
            continue;
        }
        thumbsInvalidate(br, from.string().c_str());
        ++moved;
    }
    if (moved != 0) {
        browserRefresh(br);
    }
}

// Puts the first dragged material / texture on the actor under the pointer (viewport) or the selected one (inspector).
void applyToActor(BrowserState& br, bool viewport, float px, float py) {
    const Canvas* canvas = br.canvas;
    const float dpi = canvas != nullptr && canvas->dpi >= 0.5f ? canvas->dpi : 1.0f;
    uint8_t actor = viewport ? sceneFramePick(br.scene, px * dpi, py * dpi) : kSceneActorNone;
    if (actor >= sceneFrameActorCount(br.scene)) {
        actor = sceneFrameActorSelected(br.scene);
    }
    if (actor >= sceneFrameActorCount(br.scene)) {
        spdlog::info("browser: drop needs an actor under the cursor or a selected one");
        return;
    }
    for (const fs::path& path : br.pointer.payload) {
        const FileKind kind = fileKindOf(path);
        const std::string full = path.string();
        if (kind == FileKind::Material) {
            (void)br.assignMaterial(br.app, full.c_str(), actor);
            return;
        }
        if (kind == FileKind::Texture) {
            sceneFrameQueueTexture(br.scene, actor, 0, full.c_str());
            return;
        }
    }
}

// Drag source: the grid tile (the marked set when the tile is marked) or the tree row under the press.
bool pick(void* user, float px, float py, gui::GuiDragPayload& out) {
    BrowserState& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    std::vector<fs::path>& held = b.pointer.payload;
    held.clear();
    const uint32_t tile = rowAt(br, browserGridPath(), px, py);
    const uint32_t node = tile == kGuiNoSelection ? rowAt(br, browserTreePath(), px, py) : kGuiNoSelection;
    if (tile < b.files.size()) {
        if (b.files[tile].marked) {
            for (const BrowserItem& item : b.files) {
                if (item.marked) {
                    held.push_back(item.path);
                }
            }
        } else {
            held.push_back(b.files[tile].path);
        }
    } else if (node < b.tree.size() && b.tree[node] != b.root) {
        held.push_back(b.tree[node]);
    }
    if (held.empty()) {
        return false;
    }
    if (held.size() == 1) {
        std::snprintf(out.label, sizeof(out.label), "%s", held[0].filename().string().c_str());
    } else {
        std::snprintf(out.label, sizeof(out.label), "%zu items", held.size());
    }
    const std::string first = held[0].string();
    out.len = static_cast<uint16_t>(std::min<size_t>(first.size(), sizeof(out.bytes)));
    std::memcpy(out.bytes, first.data(), out.len);
    out.kind = tile != kGuiNoSelection ? 1 : 2;
    return true;
}

void target(void* user, float px, float py, const gui::GuiDragPayload&, gui::GuiDropHit& hit) {
    BrowserState& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    const char* lists[] = {browserGridPath(), browserTreePath()};
    for (const char* list : lists) {
        const uint32_t row = rowAt(br, list, px, py);
        const bool grid = list == lists[0];
        const fs::path* dir = nullptr;
        if (grid && row < b.files.size() && b.files[row].kind == FileKind::Folder) {
            dir = &b.files[row].path;
        } else if (!grid && row < b.tree.size()) {
            dir = &b.tree[row];
        }
        if (dir != nullptr && !carried(b.pointer.payload, *dir) && guiHostListItemBox(br.gui, list, row, hit.x, hit.y, hit.w, hit.h)) {
            hit.kind = grid ? kDropGridFolder : kDropTreeFolder;
            hit.id = row;
            hit.highlight = 1;
            return;
        }
    }
    if (inside(br, kInspector, px, py)) {
        hit.kind = kDropInspector;
    } else if (!guiHostCovers(br.gui, px, py)) {
        hit.kind = kDropViewport;
    }
}

void drop(void* user, float px, float py, const gui::GuiDragPayload&, const gui::GuiDropHit& hit) {
    BrowserState& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    if (hit.kind == kDropGridFolder && hit.id < b.files.size()) {
        moveInto(br, b.files[hit.id].path);
    } else if (hit.kind == kDropTreeFolder && hit.id < b.tree.size()) {
        moveInto(br, b.tree[hit.id]);
    } else if (hit.kind == kDropInspector || hit.kind == kDropViewport) {
        applyToActor(br, hit.kind == kDropViewport, px, py);
    }
    b.pointer.payload.clear();
}

} // namespace

bool browserDragBind(BrowserState& br) {
    return guiHostBindTooltip(br.gui, browserGridPath(), tipText, &br) &&
           guiHostBindDragDrop(br.gui, kGhost, kDropBox, gui::GuiDragDropOps{.user = &br, .pick = pick, .target = target, .drop = drop});
}

void browserPointerSync(BrowserState& br) {
    guiHostDragDropStep(br.gui);
}

} // namespace burnhope
