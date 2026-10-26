#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "host/PropBind.hpp"
#include "host/PropGrid.hpp"
#include "host/PropImport.hpp"
#include "io/File.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneBlock.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Widgets.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

using namespace burnhope;

// Golden layout dumps: every property-layout asset is imported into a fresh tree, laid out at a fixed size and
// written as one line per node (name, text, box). tests/ui/golden/*.txt holds the reviewed output. An intended
// layout change is a diff of these files: rerun with BH_UPDATE_GOLDEN=1 and review the diff. The same dumps built
// with and without styles.json must match, so the C++ fallbacks stay equal to the shipped sheet.

namespace {

constexpr float kGoldenW = 1280.0f;
constexpr float kGoldenH = 2400.0f;

uint16_t hostedPlain(void*, UiState& s, UiPropBinder&, uint16_t parent, const PropSpec& spec) {
    return uiPropRow(s, parent, spec.label);
}

void dumpNode(const UiState& s, uint16_t id, int depth, std::string& out) {
    char line[256];
    char name[40] = "-";
    if (const UiName* n = s.ent[id].try_get<UiName>(); n != nullptr && n->len > 0) {
        std::snprintf(name, sizeof(name), "%.*s", static_cast<int>(n->len), n->bytes);
    }
    char text[40] = "";
    if (const UiText* t = s.ent[id].try_get<UiText>(); t != nullptr && t->len > 0) {
        std::snprintf(text, sizeof(text), "%.*s", static_cast<int>(t->len), t->bytes);
    }
    const UiBox box = uiNodeBox(s, id);
    std::snprintf(
        line, sizeof(line), "%*s%s|%s|%s %.1f,%.1f %.1fx%.1f\n", depth * 2, "", name, text, nodeShown(s, id) ? "" : "hidden", box.x, box.y, box.w, box.h);
    out += line;
    const uint32_t kids = uiNodeChildCount(s, id);
    for (uint32_t i = 0; i < kids; ++i) {
        dumpNode(s, uiNodeChild(s, id, i), depth + 1, out);
    }
}

struct Layout {
    const char* asset;
    const char* key;
    const char* golden;
};

constexpr Layout kLayouts[] = {
    {"assets/ui/editor.json", "frame", "editor_frame"},
    {"assets/ui/inspector.json", "actor", "inspector_actor"},
    {"assets/ui/inspector.json", "post", "inspector_post"},
    {"assets/ui/inspector.json", "material", "inspector_material"},
    {"assets/ui/inspector.json", "stats", "inspector_stats"},
    {"assets/ui/material.json", "material", "material_page"},
};

// Imports one asset into a fresh state and returns the dump; empty when the import failed.
std::string buildDump(const Layout& layout, bool withSheet) {
    UiState state;
    uiWidgetsInit(state);
    if (withSheet) {
        CHECK(uiStyleLoadAsset(state, "assets/ui/styles.json"));
    }
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    const PropRole roles[] = {{"matslot", hostedPlain, nullptr}, {"matrow", hostedPlain, nullptr}};
    const uint16_t root = spawn(state, kUiNone, px(kGoldenW, kGoldenH), paintRgb(0.1f, 0.1f, 0.1f));
    if (!propImportAsset(state, root, layout.asset, layout.key, binder, nullptr, 0, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr, roles, 2)) {
        return {};
    }
    uiLayout(state, kGoldenW, kGoldenH);
    std::string out;
    dumpNode(state, root, 0, out);
    return out;
}

// The property API without any JSON: one of every row builder the inspector uses.
std::string buildZooDump(bool withSheet) {
    UiState state;
    uiWidgetsInit(state);
    if (withSheet) {
        CHECK(uiStyleLoadAsset(state, "assets/ui/styles.json"));
    }
    const uint16_t root = spawn(state, kUiNone, px(kGoldenW, kGoldenH), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t host = uiPropHost(state, root);
    const uint16_t body = uiPropSection(state, host, "Zoo", 0, 0);
    (void)uiPropCheck(state, body, "Check", 0, 0, true);
    (void)uiPropNumber(state, body, "Number", 0.5f, 0.0f, 1.0f, 0.01f);
    (void)uiPropVec3(state, body, "Vector 3", 1.0f, 2.0f, 3.0f, -10.0f, 10.0f, 0.1f);
    (void)uiPropVec2(state, body, "Vector 2", 1.0f, 2.0f, -10.0f, 10.0f, 0.1f);
    (void)uiPropSwatch(state, body, "Swatch", 0.2f, 0.4f, 0.6f);
    const uint16_t menu = uiMakeMenu(state);
    (void)uiPropMenu(state, body, "Menu", "Value", menu);
    const uint16_t trigger = uiMenuButton(state, body, "Trigger", menu);
    uiSetWidthPercent(state, trigger, 100.0f);
    uiSetGrow(state, trigger, 1.0f, 1.0f);
    uiLayout(state, kGoldenW, kGoldenH);
    std::string out;
    dumpNode(state, root, 0, out);
    return out;
}

// The colour picker popup, shown, and a calendar panel: the two composite kits, built without JSON.
std::string buildKitDump(bool picker) {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(kGoldenW, kGoldenH), paintRgb(0.1f, 0.1f, 0.1f));
    if (picker) {
        uiEnsureColorPopup(state);
        if (const UiColorPickerComponent* found = uiColor(state)) {
            uiShow(state, found->colorPanel, true);
        }
    } else {
        uiShow(state, uiBuildCalendar(state, root, 20.0f, 20.0f, 280.0f, 330.0f), true);
    }
    uiLayout(state, kGoldenW, kGoldenH);
    std::string out;
    dumpNode(state, root, 0, out);
    return out;
}

bool readFile(const char* asset, std::string& text) {
    char path[512]{};
    MappedFile file{};
    if (!platformResolveAsset(asset, path, sizeof(path)) || !fileMapReadOnly(path, &file)) {
        return false;
    }
    text.assign(reinterpret_cast<const char*>(file.data), file.size);
    fileUnmap(&file);
    return true;
}

// Compares `dump` with tests/ui/golden/<name>.txt, or rewrites that file when BH_UPDATE_GOLDEN is set.
void checkGolden(const char* name, const std::string& dump) {
    char asset[128];
    std::snprintf(asset, sizeof(asset), "tests/ui/golden/%s.txt", name);
    if (std::getenv("BH_UPDATE_GOLDEN") != nullptr) {
        char path[512]{};
        CHECK(platformResolveAsset(asset, path, sizeof(path)));
        std::FILE* f = std::fopen(path, "wb");
        CHECK(f != nullptr);
        if (f == nullptr) {
            return;
        }
        std::fwrite(dump.data(), 1, dump.size(), f);
        std::fclose(f);
        return;
    }
    std::string golden;
    CHECK(readFile(asset, golden));
    CHECK_MESSAGE(golden == dump, name, " differs from ", asset, "; rerun with BH_UPDATE_GOLDEN=1 and review the diff");
}

} // namespace

TEST_CASE("golden layout dumps match the reviewed trees") {
    for (const Layout& layout : kLayouts) {
        const std::string dump = buildDump(layout, true);
        CHECK_MESSAGE(!dump.empty(), layout.golden, " failed to import");
        checkGolden(layout.golden, dump);
    }
    checkGolden("prop_zoo", buildZooDump(true));
    checkGolden("kit_color_picker", buildKitDump(true));
    checkGolden("kit_calendar", buildKitDump(false));
}

static void checkParity(const Layout& layout) {
    const std::string styled = buildDump(layout, true);
    const std::string bare = buildDump(layout, false);
    CHECK_MESSAGE(!styled.empty(), layout.golden, " failed to import");
    CHECK_MESSAGE(styled == bare, layout.golden, ": a built-in class differs from styles.json");
}

TEST_CASE("built-in style classes reproduce the shipped styles.json layout") {
    for (const Layout& layout : kLayouts) {
        checkParity(layout);
    }
    CHECK_MESSAGE(buildZooDump(true) == buildZooDump(false), "prop_zoo: a built-in class differs from styles.json");
}
