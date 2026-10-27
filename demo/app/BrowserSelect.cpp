#include <algorithm>
#include <string>

#include "app/BrowserInternal.hpp"
#include "app/MaterialController.hpp"
#include "render/SceneFrame.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Input.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kRubber = "rubber";
constexpr uint32_t kHitCap = 512;

void setMark(BrowserState& br, uint32_t index, bool on) {
    BrowserItem& item = br.files[index];
    if (item.marked != on) {
        item.marked = on;
        guiHostSetListMark(br.gui, kBrowserGrid, index, on);
    }
}

void clearMarks(BrowserState& br) {
    for (uint32_t i = 0; i < br.files.size(); ++i) {
        setMark(br, i, false);
    }
}

} // namespace

void browserOpen(BrowserState& br, const BrowserItem& item) {
    const std::string full = item.path.string();
    uint8_t first = 0;
    uint8_t after = 0;
    const uint8_t actor = sceneFrameActorSelected(br.scene);
    switch (item.kind) {
    case FileKind::Folder: browserEnter(br, item.path, true); break;
    case FileKind::Scene:
        if (item.path.extension() == ".bscene") {
            (void)sceneFrameLoad(br.scene, br.root.string().c_str(), full.c_str());
        } else {
            (void)sceneFrameLoadBhop(br.scene, full.c_str());
        }
        break;
    case FileKind::Model: (void)sceneFrameImportModel(br.scene, br.root.string().c_str(), full.c_str(), first, after); break;
    case FileKind::Texture:
        if (actor < sceneFrameActorCount(br.scene)) {
            sceneFrameQueueTexture(br.scene, actor, 0, full.c_str());
        }
        break;
    case FileKind::Material: (void)br.openMaterial(br.app, full.c_str()); break;
    default: break;
    }
}

void browserOnTree(void* user, uint32_t index, bool activate) {
    auto& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    if (index >= b.tree.size()) {
        return;
    }
    const fs::path dir = b.tree[index];
    if (!activate && guiHostPressOnChevron(br.gui, kBrowserTree, index)) {
        auto at = std::find(b.expanded.begin(), b.expanded.end(), dir);
        if (at == b.expanded.end()) {
            b.expanded.push_back(dir);
        } else {
            b.expanded.erase(at);
        }
        browserShowTree(br);
        return;
    }
    // The label opens the folder in the grid and leaves its branch unfolded.
    if (!browserContains(b.expanded, dir) && dirHasFolders(dir)) {
        b.expanded.push_back(dir);
    }
    browserEnter(br, dir, dir != b.cwd);
}

void browserOnFile(void* user, uint32_t index, bool activate) {
    auto& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    if (index >= b.files.size()) {
        return;
    }
    const uint8_t mods = guiHostPressMods(br.gui);
    const bool ctrl = (mods & 1u) != 0 && !activate;
    const bool shift = (mods & 2u) != 0 && !activate && b.anchor < b.files.size();
    if (shift) {
        const uint32_t lo = std::min(b.anchor, index);
        const uint32_t hi = std::max(b.anchor, index);
        for (uint32_t i = 0; i < b.files.size(); ++i) {
            setMark(br, i, (i >= lo && i <= hi) || (ctrl && b.files[i].marked));
        }
    } else if (ctrl) {
        setMark(br, index, !b.files[index].marked);
        b.anchor = index;
    } else if (!b.files[index].marked || activate || mods == 0) {
        clearMarks(br);
        setMark(br, index, true);
        b.anchor = index;
    }
    b.selected = index;
    browserShowStatus(br);
    if (activate) {
        browserOpen(br, b.files[index]);
    }
}

void browserOnContext(void* user, uint32_t index) {
    auto& br = *static_cast<BrowserState*>(user);
    if (index < br.files.size()) {
        br.context = index;
        if (!br.files[index].marked) {
            browserOnFile(user, index, false);
        }
        guiHostOpenAtPointer(br.gui, "ctxMenu");
    }
}

// Marquee: a left drag that starts on empty grid space selects every tile the rectangle meets.
void browserPollMarquee(BrowserState& br) {
    BrowserState& b = br;
    float px = 0.0f;
    float py = 0.0f;
    uint32_t down = 0;
    guiHostPointer(br.gui, px, py, down);
    const bool left = (down & kPointerLeft) != 0;
    const bool ctrl = (guiHostPressMods(br.gui) & 1u) != 0;
    if (left && !b.prevLeft && guiHostPointerOn(br.gui, kBrowserGrid)) {
        b.armed = true;
        b.rubbing = false;
        b.rubX = px;
        b.rubY = py;
        b.base.assign(b.files.size(), 0);
        for (size_t i = 0; ctrl && i < b.files.size(); ++i) {
            b.base[i] = b.files[i].marked ? 1 : 0;
        }
    }
    b.prevLeft = left;
    if (!b.armed) {
        return;
    }
    if (left) {
        const float dx = px - b.rubX;
        const float dy = py - b.rubY;
        const float deadzone = guiHostToken(br.gui, "browser.marquee");
        b.rubbing = b.rubbing || dx * dx + dy * dy > deadzone * deadzone;
        float gx = 0.0f;
        float gy = 0.0f;
        float gw = 0.0f;
        float gh = 0.0f;
        if (!b.rubbing || !guiHostBox(br.gui, kBrowserGrid, gx, gy, gw, gh)) {
            return;
        }
        const float x0 = std::clamp(std::min(b.rubX, px), gx, gx + gw);
        const float x1 = std::clamp(std::max(b.rubX, px), gx, gx + gw);
        const float y0 = std::clamp(std::min(b.rubY, py), gy, gy + gh);
        const float y1 = std::clamp(std::max(b.rubY, py), gy, gy + gh);
        guiHostShowMarquee(br.gui, kRubber, x0, y0, x1, y1);
        uint32_t hits[kHitCap];
        const uint32_t n = guiHostListHits(br.gui, kBrowserGrid, x0, y0, x1, y1, hits, kHitCap);
        for (uint32_t i = 0; i < b.files.size() && i < b.base.size(); ++i) {
            setMark(br, i, b.base[i] != 0);
        }
        for (uint32_t k = 0; k < n; ++k) {
            if (hits[k] < b.files.size()) {
                setMark(br, hits[k], true);
            }
        }
        return;
    }
    if (!b.rubbing && !ctrl) {
        clearMarks(br); // a plain click on empty space drops the selection
    }
    b.armed = false;
    b.rubbing = false;
    guiHostSetShown(br.gui, kRubber, false);
    b.selected = kGuiNoSelection;
    browserShowStatus(br);
}

} // namespace burnhope
