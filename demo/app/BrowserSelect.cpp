#include <algorithm>
#include <string>

#include "app/BrowserInternal.hpp"
#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "app/MaterialController.hpp"
#include "render/SceneFrame.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Input.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kRubber = "rubber";
constexpr uint32_t kHitCap = 512;

void setMark(Engine& e, uint32_t index, bool on) {
    BrowserItem& item = e.browser.files[index];
    if (item.marked != on) {
        item.marked = on;
        guiHostSetListMark(e.gui, kBrowserGrid, index, on);
    }
}

void clearMarks(Engine& e) {
    for (uint32_t i = 0; i < e.browser.files.size(); ++i) {
        setMark(e, i, false);
    }
}

} // namespace

void browserOpen(Engine& e, const BrowserItem& item) {
    const std::string full = item.path.string();
    uint8_t first = 0;
    uint8_t after = 0;
    const uint8_t actor = sceneFrameActorSelected(e.scene);
    switch (item.kind) {
    case FileKind::Folder: browserEnter(e, item.path, true); break;
    case FileKind::Scene:
        if (item.path.extension() == ".bscene") {
            (void)sceneFrameLoad(e.scene, e.browser.root.string().c_str(), full.c_str());
        } else {
            (void)sceneFrameLoadBhop(e.scene, full.c_str());
        }
        break;
    case FileKind::Model: (void)sceneFrameImportModel(e.scene, e.browser.root.string().c_str(), full.c_str(), first, after); break;
    case FileKind::Texture:
        if (actor < sceneFrameActorCount(e.scene)) {
            sceneFrameQueueTexture(e.scene, actor, 0, full.c_str());
        }
        break;
    case FileKind::Material: (void)materialOpen(e, full.c_str()); break;
    default: break;
    }
}

void browserOnTree(void* user, uint32_t index, bool activate) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (index >= b.tree.size()) {
        return;
    }
    const fs::path dir = b.tree[index];
    if (!activate && guiHostPressOnChevron(e.gui, kBrowserTree, index)) {
        auto at = std::find(b.expanded.begin(), b.expanded.end(), dir);
        if (at == b.expanded.end()) {
            b.expanded.push_back(dir);
        } else {
            b.expanded.erase(at);
        }
        browserShowTree(e);
        return;
    }
    // The label opens the folder in the grid and leaves its branch unfolded.
    if (!browserContains(b.expanded, dir) && dirHasFolders(dir)) {
        b.expanded.push_back(dir);
    }
    browserEnter(e, dir, dir != b.cwd);
}

void browserOnFile(void* user, uint32_t index, bool activate) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (index >= b.files.size()) {
        return;
    }
    const uint8_t mods = guiHostPressMods(e.gui);
    const bool ctrl = (mods & 1u) != 0 && !activate;
    const bool shift = (mods & 2u) != 0 && !activate && b.anchor < b.files.size();
    if (shift) {
        const uint32_t lo = std::min(b.anchor, index);
        const uint32_t hi = std::max(b.anchor, index);
        for (uint32_t i = 0; i < b.files.size(); ++i) {
            setMark(e, i, (i >= lo && i <= hi) || (ctrl && b.files[i].marked));
        }
    } else if (ctrl) {
        setMark(e, index, !b.files[index].marked);
        b.anchor = index;
    } else if (!b.files[index].marked || activate || mods == 0) {
        clearMarks(e);
        setMark(e, index, true);
        b.anchor = index;
    }
    b.selected = index;
    browserShowStatus(e);
    if (activate) {
        browserOpen(e, b.files[index]);
    }
}

void browserOnContext(void* user, uint32_t index) {
    auto& e = *static_cast<Engine*>(user);
    if (index < e.browser.files.size()) {
        e.browser.context = index;
        if (!e.browser.files[index].marked) {
            browserOnFile(user, index, false);
        }
        guiHostOpenAtPointer(e.gui, "ctxMenu");
    }
}

// Marquee: a left drag that starts on empty grid space selects every tile the rectangle meets.
void browserPollMarquee(Engine& e) {
    BrowserState& b = e.browser;
    float px = 0.0f;
    float py = 0.0f;
    uint32_t down = 0;
    guiHostPointer(e.gui, px, py, down);
    const bool left = (down & kPointerLeft) != 0;
    const bool ctrl = (guiHostPressMods(e.gui) & 1u) != 0;
    if (left && !b.prevLeft && guiHostPointerOn(e.gui, kBrowserGrid)) {
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
        const float deadzone = guiHostToken(e.gui, "browser.marquee");
        b.rubbing = b.rubbing || dx * dx + dy * dy > deadzone * deadzone;
        float gx = 0.0f;
        float gy = 0.0f;
        float gw = 0.0f;
        float gh = 0.0f;
        if (!b.rubbing || !guiHostBox(e.gui, kBrowserGrid, gx, gy, gw, gh)) {
            return;
        }
        const float x0 = std::clamp(std::min(b.rubX, px), gx, gx + gw);
        const float x1 = std::clamp(std::max(b.rubX, px), gx, gx + gw);
        const float y0 = std::clamp(std::min(b.rubY, py), gy, gy + gh);
        const float y1 = std::clamp(std::max(b.rubY, py), gy, gy + gh);
        guiHostSetRect(e.gui, kRubber, x0, y0, x1 - x0, y1 - y0);
        guiHostSetShown(e.gui, kRubber, true);
        uint32_t hits[kHitCap];
        const uint32_t n = guiHostListHits(e.gui, kBrowserGrid, x0, y0, x1, y1, hits, kHitCap);
        for (uint32_t i = 0; i < b.files.size() && i < b.base.size(); ++i) {
            setMark(e, i, b.base[i] != 0);
        }
        for (uint32_t k = 0; k < n; ++k) {
            if (hits[k] < b.files.size()) {
                setMark(e, hits[k], true);
            }
        }
        return;
    }
    if (!b.rubbing && !ctrl) {
        clearMarks(e); // a plain click on empty space drops the selection
    }
    b.armed = false;
    b.rubbing = false;
    guiHostSetShown(e.gui, kRubber, false);
    b.selected = kGuiNoSelection;
    browserShowStatus(e);
}

} // namespace burnhope
