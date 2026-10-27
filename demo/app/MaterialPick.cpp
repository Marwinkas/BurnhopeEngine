#include "app/MaterialInternal.hpp"

#include "app/BrowserModel.hpp"
#include "app/BrowserThumbs.hpp"
#include "gui/GuiHost.hpp"
#include "Str.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>

namespace burnhope {
namespace {

// One slot card: the atlas cell of its map, the path field of the document it edits and its JSON "bind" keys.
struct Slot {
    uint8_t cell;
    char (BhmatDoc::*path)[260];
    const char* thumb;
    const char* card;
    bool classic; // card is part of the Classic workflow only
    bool packed;  // card is part of the ORMH workflow only
};

constexpr Slot kSlots[kMatSlotCount] = {
    {0, &BhmatDoc::albedoPath, "mat.albedo.thumb", "mat.albedo.card", false, false},
    {3, &BhmatDoc::normalPath, "mat.normal.thumb", "mat.normal.card", false, false},
    {1, &BhmatDoc::metalPath, "mat.metal.thumb", "mat.metal.card", true, false},
    {4, &BhmatDoc::heightPath, "mat.height.thumb", "mat.height.card", true, false},
    {8, &BhmatDoc::alphaPath, "mat.alpha.thumb", "mat.alpha.card", false, false},
    {2, &BhmatDoc::roughPath, "mat.rough.thumb", "mat.rough.card", true, false},
    {5, &BhmatDoc::aoPath, "mat.ao.thumb", "mat.ao.card", true, false},
    {7, &BhmatDoc::emissivePath, "mat.emission.thumb", "mat.emission.card", false, false},
    {6, &BhmatDoc::ormPath, "mat.orm.thumb", "mat.orm.card", false, true},
};

constexpr const char* kSwatchKey[2] = {"mat.albedo.swatch", "mat.emission.swatch"};
constexpr const char* kPicker = "texPicker";
constexpr const char* kList = "mat.tex.list";
constexpr const char* kSearch = "mat.tex.search";
constexpr uint32_t kScanVisitCap = 20000; // directory entries one scan looks at
constexpr uint32_t kScanDepthCap = 6;
constexpr uint32_t kPickTilePx = 48;      // thumbnail edge of a picker tile
constexpr uint32_t kPickBakesPerFrame = 3;

namespace fs = std::filesystem;

// Textures below the project root (the material's folder when no project is open), sorted by path.
void scanTextures(MaterialState& m) {
    m.textures.clear();
    const fs::path base = !m.root.empty() ? m.root : fs::path(m.path).parent_path();
    std::error_code ec;
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    uint32_t visited = 0;
    for (; !ec && it != fs::recursive_directory_iterator() && visited < kScanVisitCap; it.increment(ec)) {
        ++visited;
        const fs::path& p = it->path();
        if (!p.filename().empty() && p.filename().string().front() == '.') {
            it.disable_recursion_pending();
            continue;
        }
        if (it.depth() >= static_cast<int>(kScanDepthCap)) {
            it.disable_recursion_pending();
        }
        if (it->is_regular_file(ec) && fileKindOf(p) == FileKind::Texture) {
            m.textures.push_back(p);
        }
    }
    std::sort(m.textures.begin(), m.textures.end());
}

bool containsNoCase(const std::string& hay, const char* needle) {
    const std::string_view n(needle);
    return std::search(hay.begin(), hay.end(), n.begin(), n.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           }) != hay.end();
}

// Fills the picker grid: "(None)" first, then every texture whose file name contains the search text.
void rebuild(MaterialState& m) {
    m.shown.clear();
    for (uint32_t i = 0; i < m.textures.size() && m.shown.size() + 1 < kGuiListCap; ++i) {
        if (m.search[0] == '\0' || containsNoCase(m.textures[i].filename().string(), m.search)) {
            m.shown.push_back(i);
        }
    }
    const uint32_t count = static_cast<uint32_t>(m.shown.size()) + 1;
    auto rows = std::make_unique<GuiListEntry[]>(count);
    std::snprintf(rows[0].text, sizeof(rows[0].text), "(None)\nClear slot");
    for (uint32_t i = 1; i < count; ++i) {
        const fs::path& p = m.textures[m.shown[i - 1]];
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::snprintf(rows[i].text, sizeof(rows[i].text), "%s\n%s", p.filename().string().c_str(), ext.c_str() + (ext.empty() ? 0 : 1));
    }
    m.cells.assign(count, 0);
    guiHostSetList(m.gui, kList, rows.get(), count);
}

void onSlot(void* user, uint32_t action) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    const uint32_t slot = action - kActMatSlot;
    if (slot >= kMatSlotCount) {
        return;
    }
    m.pickSlot = static_cast<uint8_t>(slot);
    m.search[0] = '\0';
    guiHostSetText(m.gui, kSearch, "");
    scanTextures(m);
    rebuild(m);
    guiHostSetListThumbs(m.gui, kList, kThumbSlot, kThumbGrid, kThumbGrid, kPickTilePx);
    guiHostSetListMetrics(m.gui, kList, kPickTilePx + guiHostToken(m.gui, "tile.gutter"), kPickTilePx + guiHostToken(m.gui, "tile.caption"), true);
    guiHostOpenAt(m.gui, kPicker, kSlots[slot].thumb);
}

void onSearch(void* user, const char* text) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    if (m.pickSlot >= kMatSlotCount || std::string_view(text) == m.search) {
        return;
    }
    (void)strCopyCap(m.search, sizeof(m.search), text);
    rebuild(m);
}

void onPick(void* user, uint32_t index, bool) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    if (m.pickSlot >= kMatSlotCount || index > m.shown.size()) {
        return;
    }
    char* dst = m.doc.*kSlots[m.pickSlot].path;
    const std::string path = index == 0 ? std::string() : m.textures[m.shown[index - 1]].generic_string();
    (void)strCopyCap(dst, 260, path.c_str());
    m.pickSlot = 0xff;
    guiHostClose(m.gui, kPicker);
    materialPushSlots(m);
    materialTouchAtlas(m);
}

void onColor(void* user, float r, float g, float b) {
    auto* f = static_cast<MaterialField*>(user);
    const float rgb[3] = {r, g, b};
    materialSetColor(*f->state, f->index, rgb);
}

void onSwatch(void* user, uint32_t action) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    const uint32_t which = action - kActMatSwatch;
    if (which < 2) {
        const float(&rgb)[3] = which == 0 ? m.doc.base : m.doc.emissiveColor;
        (void)guiHostOpenColorPicker(m.gui, kSwatchKey[which], rgb, onColor, &m.swatch[which]);
    }
}

} // namespace

void materialPushSlots(MaterialState& m) {
    const bool packed = m.doc.mapPack != 0;
    for (const Slot& s : kSlots) {
        const bool filled = (m.doc.*s.path)[0] != '\0';
        guiHostSetPhoto(m.gui, s.thumb, filled ? 9 : 255, s.cell, 4, 3);
        guiHostSetText(m.gui, s.thumb, filled ? "" : "None");
        if (s.classic || s.packed) {
            guiHostSetShown(m.gui, s.card, s.packed ? packed : !packed);
        }
    }
}

void materialPickSync(MaterialState& m) {
    if (m.pickSlot >= kMatSlotCount) {
        return;
    }
    if (!guiHostIsOpen(m.gui, kPicker)) { // light-dismissed: the picker closed itself
        m.pickSlot = 0xff;
        return;
    }
    if (m.thumb == nullptr) {
        return;
    }
    uint32_t first = 0;
    uint32_t live = 0;
    guiHostListWindow(m.gui, kList, first, live);
    uint32_t budget = kPickBakesPerFrame;
    for (uint32_t i = std::max(first, 1u); i < first + live && i <= m.shown.size() && i < m.cells.size(); ++i) {
        const std::string path = m.textures[m.shown[i - 1]].generic_string();
        const uint8_t cell = m.thumb(m.app, path.c_str(), budget);
        if (cell != 0 && cell != m.cells[i]) {
            m.cells[i] = cell;
            guiHostSetListThumb(m.gui, kList, i, cell);
        }
    }
}

bool materialBindPickers(MaterialState& m) {
    bool ok = true;
    for (uint32_t i = 0; i < kMatSlotCount; ++i) {
        ok = guiHostBindClick(m.gui, kActMatSlot + i, onSlot, &m) && ok;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        m.swatch[i] = {&m, i};
        ok = guiHostBindClick(m.gui, kActMatSwatch + i, onSwatch, &m) && ok;
    }
    ok = guiHostBindEdit(m.gui, kSearch, onSearch, &m) && ok;
    guiHostSetPlaceholder(m.gui, kSearch, "Search textures...");
    return guiHostBindList(m.gui, kList, onPick, &m) && ok;
}

} // namespace burnhope
