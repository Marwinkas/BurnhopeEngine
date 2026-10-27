#include "app/BrowserController.hpp"

#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "gpu_scene/SceneObject.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kTree = "bottom::treeCol::tree";
constexpr const char* kGrid = "bottom::filesCol::grid";
constexpr const char* kPath = "bottom::filesCol::bar::path";
constexpr const char* kSearch = "bottom::filesCol::bar::search";
constexpr uint32_t kTreeDepth = 2;
constexpr uint32_t kTreeCap = 400;

enum Kind : uint8_t { kFolder, kScene, kModel, kTexture, kOther };

Kind kindOf(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        return kFolder;
    }
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".bhop" || ext == ".bscene") return kScene;
    if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") return kModel;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga") return kTexture;
    return kOther;
}

bool listed(const fs::path& p) {
    const std::string name = p.filename().string();
    return !name.empty() && name[0] != '.' && name != "build-core" && name != "cache";
}

bool matches(const std::string& name, const char* filter) {
    if (filter[0] == '\0') {
        return true;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(needle) != std::string::npos;
}

void put(std::vector<GuiListEntry>& rows, const std::string& text, uint8_t tone) {
    GuiListEntry row{};
    std::strncpy(row.text, text.c_str(), kGuiListTextCap - 1);
    row.tone = tone;
    rows.push_back(row);
}

void scanTree(BrowserState& b, const fs::path& dir, uint32_t depth) {
    if (b.tree.size() >= kTreeCap) {
        return;
    }
    b.tree.push_back(dir);
    put(b.rows, std::string(depth * 2, ' ') + dir.filename().string() + "/", 0);
    if (depth >= kTreeDepth) {
        return;
    }
    std::error_code ec;
    std::vector<fs::path> children;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec) && listed(entry.path())) {
            children.push_back(entry.path());
        }
    }
    std::sort(children.begin(), children.end());
    for (const fs::path& child : children) {
        scanTree(b, child, depth + 1);
    }
}

void showFiles(Engine& e) {
    BrowserState& b = e.browser;
    b.files.clear();
    b.rows.clear();
    std::error_code ec;
    std::vector<BrowserItem> all;
    for (const fs::directory_entry& entry : fs::directory_iterator(b.cwd, ec)) {
        if (listed(entry.path()) && matches(entry.path().filename().string(), b.filter)) {
            all.push_back({entry.path(), kindOf(entry.path())});
        }
    }
    std::sort(all.begin(), all.end(), [](const BrowserItem& l, const BrowserItem& r) {
        const bool lf = l.kind == kFolder;
        if (lf != (r.kind == kFolder)) {
            return lf;
        }
        return l.kind != r.kind ? l.kind < r.kind : l.path < r.path;
    });
    for (const BrowserItem& item : all) {
        const bool folder = item.kind == kFolder;
        put(b.rows, item.path.filename().string() + (folder ? "/" : ""), folder ? 1 : (item.kind == kOther ? 0 : 2));
        b.files.push_back(item);
    }
    guiHostSetList(e.gui, kGrid, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
    guiHostSetText(e.gui, kPath, fs::relative(b.cwd, b.root, ec).string().c_str());
}

void showTree(Engine& e) {
    BrowserState& b = e.browser;
    b.tree.clear();
    b.rows.clear();
    scanTree(b, b.root, 0);
    guiHostSetList(e.gui, kTree, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
    for (uint32_t i = 0; i < b.tree.size(); ++i) {
        if (b.tree[i] == b.cwd) {
            guiHostSetListSelected(e.gui, kTree, i);
        }
    }
}

void enter(Engine& e, const fs::path& dir) {
    e.browser.cwd = dir;
    showFiles(e);
    for (uint32_t i = 0; i < e.browser.tree.size(); ++i) {
        guiHostSetListSelected(e.gui, kTree, e.browser.tree[i] == dir ? i : kGuiNoSelection);
        if (e.browser.tree[i] == dir) {
            break;
        }
    }
}

void open(Engine& e, const BrowserItem& item) {
    const std::string full = item.path.string();
    uint8_t first = 0;
    uint8_t after = 0;
    const uint8_t actor = sceneFrameActorSelected(e.scene);
    switch (item.kind) {
    case kFolder: enter(e, item.path); break;
    case kScene:
        if (item.path.extension() == ".bscene") {
            (void)sceneFrameLoad(e.scene, e.browser.root.string().c_str(), full.c_str());
        } else {
            (void)sceneFrameLoadBhop(e.scene, full.c_str());
        }
        break;
    case kModel: (void)sceneFrameImportModel(e.scene, e.browser.root.string().c_str(), full.c_str(), first, after); break;
    case kTexture:
        if (actor < sceneFrameActorCount(e.scene)) {
            sceneFrameQueueTexture(e.scene, actor, 0, full.c_str());
        }
        break;
    default: break;
    }
}

void onTree(void* user, uint32_t index, bool) {
    auto& e = *static_cast<Engine*>(user);
    if (index < e.browser.tree.size()) {
        enter(e, e.browser.tree[index]);
    }
}

void onFile(void* user, uint32_t index, bool activate) {
    auto& e = *static_cast<Engine*>(user);
    if (index >= e.browser.files.size()) {
        return;
    }
    guiHostSetListSelected(e.gui, kGrid, index);
    if (activate) {
        open(e, e.browser.files[index]);
    }
}

void onUp(void* user, uint32_t) {
    auto& e = *static_cast<Engine*>(user);
    if (e.browser.cwd != e.browser.root && e.browser.cwd.has_parent_path()) {
        enter(e, e.browser.cwd.parent_path());
    }
}

void onSearch(void* user, const char* text) {
    auto& e = *static_cast<Engine*>(user);
    std::strncpy(e.browser.filter, text, sizeof(e.browser.filter) - 1);
    showFiles(e);
}

} // namespace

bool browserBind(Engine& e) {
    std::error_code ec;
    e.browser.engine = &e;
    e.browser.root = fs::current_path(ec);
    e.browser.cwd = e.browser.root;
    const bool ok = guiHostBindList(e.gui, kTree, onTree, &e) && guiHostBindList(e.gui, kGrid, onFile, &e) &&
                    guiHostBindClick(e.gui, kActFolderUp, onUp, &e) && guiHostBindEdit(e.gui, kSearch, onSearch, &e);
    showTree(e);
    showFiles(e);
    return ok;
}

} // namespace burnhope
