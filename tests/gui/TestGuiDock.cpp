#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include <string_view>

#include "Gui.hpp"
#include "interaction/InteractSplitter.hpp"
#include "io/File.hpp"
#include "layout/GuiDock.hpp"
#include "layout/GuiLayoutEngine.hpp"
#include "loader/GuiLoader.hpp"
#include "style/GuiStyleSheet.hpp"

using namespace burnhope;
using namespace burnhope::gui;

// Exceptions are disabled, so a failed precondition ends the test case with a plain return.
#define NEED(cond)               \
    do {                         \
        const bool needOk = (cond); \
        CHECK(needOk);           \
        if (!needOk) {           \
            return;              \
        }                        \
    } while (0)

namespace {

constexpr float kW = 1000.0f;
constexpr float kH = 600.0f;

constexpr const char* kSplitScreen = R"({
  "name": "scr", "class": "screen",
  "children": [
    { "name": "row", "class": "main_row", "children": [
      { "name": "view", "class": "viewport" },
      { "name": "grip", "class": "splitter_v", "splitter": "x", "resize": "next", "splitMin": 150, "splitMax": 300 },
      { "name": "side", "class": "dock_tree" }
    ] }
  ]
})";

constexpr const char* kDockScreen = R"({
  "name": "scr", "class": "screen",
  "children": [
    { "name": "row", "class": "main_row", "children": [
      { "name": "a", "class": "dock_right", "dock": true, "children": [
        { "name": "p1", "class": "dock_panel", "tab": "One", "tabIcon": "gear" },
        { "name": "p2", "class": "dock_panel", "tab": "Two" }
      ] },
      { "name": "b", "class": "dock_right", "dock": true, "children": [
        { "name": "p3", "class": "dock_panel", "tab": "Three" },
        { "name": "p4", "class": "dock_panel", "tab": "Four", "closed": true }
      ] }
    ] }
  ]
})";

struct Rig {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    float px = 0.0f;
    float py = 0.0f;
    bool held = false;

    bool load(const char* screen) {
        ctx.style = &sheet;
        if (!guiStyleLoadFile(sheet, "assets/ui/styles.json") || !guiInit(ctx, world) ||
            !guiLoadScreen(world, sheet, screen, flecs::entity(), &root)) {
            return false;
        }
        guiLayout(ctx, kW, kH);
        guiLayout(ctx, kW, kH);
        return true;
    }
    ~Rig() {
        if (root.is_valid() && root.is_alive()) {
            guiLayoutDetach(root);
        }
    }
    void step(float x, float y, bool down) {
        InputFrame in{};
        in.pointer.x = x;
        in.pointer.y = y;
        in.pointer.dx = x - px;
        in.pointer.dy = y - py;
        in.pointer.down = down ? kPointerLeft : 0;
        in.pointer.pressed = down && !held ? kPointerLeft : 0;
        in.pointer.released = !down && held ? kPointerLeft : 0;
        px = x;
        py = y;
        held = down;
        guiConsumeInput(ctx, in);
        guiLayout(ctx, kW, kH);
        guiLayout(ctx, kW, kH);
    }
    flecs::entity at(const char* path) { return world.lookup(path); }
    flecs::entity tabOf(const char* dockPath, const char* panelPath) {
        const flecs::entity panel = at(panelPath);
        flecs::entity found;
        at(dockPath).lookup("tabs").children([&](flecs::entity c) {
            if (const UiTab* t = c.try_get<UiTab>(); t != nullptr && t->panel == panel.id()) {
                found = c;
            }
        });
        return found;
    }
    static void center(flecs::entity e, float& x, float& y) {
        const UiBox b = *e.try_get<UiBox>();
        x = b.x + b.w * 0.5f;
        y = b.y + b.h * 0.5f;
    }
};

} // namespace

TEST_CASE("splitter: dragging the grip resizes the sibling, clamps it and asks for the resize cursor") {
    Rig rig;
    NEED(rig.load(kSplitScreen));
    const flecs::entity side = rig.at("scr::row::side");
    const flecs::entity grip = rig.at("scr::row::grip");
    NEED(side.is_valid());
    NEED(grip.is_valid());
    CHECK(side.try_get<UiBox>()->w == doctest::Approx(240.0f));
    float gx = 0.0f;
    float gy = 0.0f;
    Rig::center(grip, gx, gy);

    rig.step(gx, gy, false);
    CHECK(rig.ctx.interact.cursor == kGuiCursorEastWest);
    rig.step(gx, gy, true);
    CHECK(rig.ctx.interact.split == grip.id());
    rig.step(gx - 50.0f, gy, true);
    CHECK(side.try_get<UiBox>()->w == doctest::Approx(290.0f));
    rig.step(gx - 500.0f, gy, true);
    CHECK(side.try_get<UiBox>()->w == doctest::Approx(300.0f));
    rig.step(gx + 500.0f, gy, true);
    CHECK(side.try_get<UiBox>()->w == doctest::Approx(150.0f));
    rig.step(gx - 20.0f, gy, false);
    CHECK(rig.ctx.interact.split == 0);
    rig.step(gx - 300.0f, gy, false);
    CHECK(side.try_get<UiBox>()->w == doctest::Approx(150.0f));
}

TEST_CASE("dock: one panel shows, a press on a tab selects, the close button hides the panel and its tab") {
    Rig rig;
    NEED(rig.load(kDockScreen));
    const flecs::entity p1 = rig.at("scr::row::a::p1");
    const flecs::entity p2 = rig.at("scr::row::a::p2");
    const flecs::entity dockB = rig.at("scr::row::b");
    NEED(p1.is_valid());
    NEED(p2.is_valid());
    CHECK(!p1.has<UiHidden>());
    CHECK(p2.has<UiHidden>());
    CHECK(rig.at("scr::row::b::p4").has<UiHidden>()); // closed from the JSON

    const flecs::entity tab2 = rig.tabOf("scr::row::a", "scr::row::a::p2");
    NEED(tab2.is_valid());
    CHECK(tab2.try_get<UiBox>()->w > kGuiTabPadL + kGuiTabPadR);
    float x = 0.0f;
    float y = 0.0f;
    Rig::center(tab2, x, y);
    x -= 10.0f; // left of the close button
    rig.step(x, y, true);
    rig.step(x, y, false);
    CHECK(p1.has<UiHidden>());
    CHECK(!p2.has<UiHidden>());

    flecs::entity closeBtn;
    tab2.children([&](flecs::entity c) {
        if (c.has<UiTabClose>()) {
            closeBtn = c;
        }
    });
    NEED(closeBtn.is_valid());
    Rig::center(closeBtn, x, y);
    rig.step(x, y, true);
    rig.step(x, y, false);
    CHECK(tab2.has<UiHidden>());
    CHECK(p2.has<UiHidden>());
    CHECK(!p1.has<UiHidden>()); // the other panel takes over

    // closing the last open panel hides the dock; showing the dock again brings its first panel back
    const flecs::entity tab1 = rig.tabOf("scr::row::a", "scr::row::a::p1");
    flecs::entity close1;
    tab1.children([&](flecs::entity c) {
        if (c.has<UiTabClose>()) {
            close1 = c;
        }
    });
    Rig::center(close1, x, y);
    rig.step(x, y, true);
    rig.step(x, y, false);
    CHECK(rig.at("scr::row::a").has<UiHidden>());
    CHECK(!dockB.has<UiHidden>());
    guiSetShown(rig.at("scr::row::a"), true);
    rig.step(5.0f, 5.0f, false);
    CHECK(!rig.at("scr::row::a").has<UiHidden>());
    CHECK(!p1.has<UiHidden>());
}

TEST_CASE("dock: dragging a tab onto another dock's tab bar moves the tab and its panel there") {
    Rig rig;
    NEED(rig.load(kDockScreen));
    const flecs::entity p2 = rig.at("scr::row::a::p2");
    const flecs::entity dockA = rig.at("scr::row::a");
    const flecs::entity dockB = rig.at("scr::row::b");
    const flecs::entity tab2 = rig.tabOf("scr::row::a", "scr::row::a::p2");
    NEED(tab2.is_valid());
    float x = 0.0f;
    float y = 0.0f;
    Rig::center(tab2, x, y);
    x -= 10.0f;
    float bx = 0.0f;
    float by = 0.0f;
    const flecs::entity barB = dockB.lookup("tabs");
    NEED(barB.is_valid());
    const UiBox bb = *barB.try_get<UiBox>();
    bx = bb.x + bb.w - 4.0f; // right end of the bar: the dropped tab goes last
    by = bb.y + bb.h * 0.5f;

    rig.step(x, y, true);
    rig.step(x + 30.0f, y + 2.0f, true);
    CHECK(rig.ctx.interact.tabPhase == 2);
    CHECK(rig.ctx.interact.cursor == kGuiCursorMove);
    rig.step(bx, by, true);
    CHECK(rig.ctx.interact.tabBar == barB.id());
    rig.step(bx, by, false);

    CHECK(p2.parent() == dockB);
    CHECK(tab2.parent() == barB);
    CHECK(rig.ctx.interact.tabBar == 0);
    CHECK(!p2.has<UiHidden>());                          // the moved panel is the selected one of its new dock
    CHECK(rig.at("scr::row::b::p3").has<UiHidden>());
    CHECK(!rig.at("scr::row::a::p1").has<UiHidden>());   // the old dock falls back to its remaining panel
    CHECK(dockA.try_get<UiDock>()->active == rig.at("scr::row::a::p1").id());
}

TEST_CASE("dock: Escape cancels a live tab drag") {
    Rig rig;
    NEED(rig.load(kDockScreen));
    const flecs::entity p2 = rig.at("scr::row::a::p2");
    const flecs::entity tab2 = rig.tabOf("scr::row::a", "scr::row::a::p2");
    float x = 0.0f;
    float y = 0.0f;
    Rig::center(tab2, x, y);
    x -= 10.0f;
    const UiBox bb = *rig.at("scr::row::b").lookup("tabs").try_get<UiBox>();
    rig.step(x, y, true);
    rig.step(bb.x + 20.0f, bb.y + 5.0f, true);
    CHECK(rig.ctx.interact.tabPhase == 2);
    InputFrame esc{};
    esc.pointer.x = bb.x + 20.0f;
    esc.pointer.y = bb.y + 5.0f;
    esc.pointer.down = kPointerLeft;
    esc.escape = 1;
    guiConsumeInput(rig.ctx, esc);
    CHECK(rig.ctx.interact.tabPhase == 0);
    rig.step(bb.x + 20.0f, bb.y + 5.0f, false);
    CHECK(p2.parent() == rig.at("scr::row::a"));
}

TEST_CASE("real shell: tabbed docks and splitters mount and the bottom splitter resizes the content browser") {
    Rig rig;
    rig.ctx.style = &rig.sheet;
    NEED(guiStyleLoadFile(rig.sheet, "assets/ui/styles.json"));
    NEED(guiInit(rig.ctx, rig.world));
    MappedFile file{};
    NEED(fileMapReadOnly("assets/gui/shell.json", &file));
    const bool loaded = guiLoadScreen(rig.world, rig.sheet, std::string_view{reinterpret_cast<const char*>(file.data), file.size},
                                      flecs::entity(), &rig.root);
    fileUnmap(&file);
    NEED(loaded);
    guiLayout(rig.ctx, kW, kH);
    guiLayout(rig.ctx, kW, kH);
    const flecs::entity inspector = rig.at("shell::main::rightDock::inspector");
    NEED(inspector.is_valid());
    // The material editor mounts later as one more panel of the right dock; it starts closed.
    MappedFile mat{};
    NEED(fileMapReadOnly("assets/gui/material.json", &mat));
    flecs::entity material;
    const bool spawned = guiLoadScreen(rig.world, rig.sheet, std::string_view{reinterpret_cast<const char*>(mat.data), mat.size},
                                       rig.at("shell::main::rightDock"), &material);
    fileUnmap(&mat);
    NEED(spawned);
    guiLayout(rig.ctx, kW, kH);
    guiLayout(rig.ctx, kW, kH);
    CHECK(inspector.has<UiDockPanel>());
    CHECK(!inspector.has<UiHidden>());
    CHECK(material.has<UiDockPanel>());
    CHECK(material.has<UiHidden>());
    CHECK(material.has<UiHidden>());
    guiDockOpen(rig.world, material);
    guiLayout(rig.ctx, kW, kH);
    CHECK(!material.has<UiHidden>());
    CHECK(inspector.has<UiHidden>());
    guiDockOpen(rig.world, inspector);
    guiLayout(rig.ctx, kW, kH);

    // The texture picker is a modal: centred whatever the anchor.
    const flecs::entity picker = rig.at("shell::texPicker");
    NEED(picker.is_valid());
    picker.try_get_mut<UiOverlay>()->open = 1;
    guiLayout(rig.ctx, kW, kH);
    guiLayout(rig.ctx, kW, kH);
    const UiBox pb = *picker.try_get<UiBox>();
    CHECK(pb.w > 100.0f);
    CHECK(pb.x + pb.w * 0.5f == doctest::Approx(kW * 0.5f).epsilon(0.01));
    CHECK(pb.y + pb.h * 0.5f == doctest::Approx(kH * 0.5f).epsilon(0.01));
    picker.try_get_mut<UiOverlay>()->open = 0;
    guiLayout(rig.ctx, kW, kH);

    const flecs::entity bottom = rig.at("shell::bottom");
    const flecs::entity grip = rig.at("shell::splitBottom");
    NEED(bottom.is_valid());
    NEED(grip.is_valid());
    const float before = bottom.try_get<UiBox>()->h;
    float gx = 0.0f;
    float gy = 0.0f;
    Rig::center(grip, gx, gy);
    rig.step(gx, gy, true);
    rig.step(gx, gy - 60.0f, true);
    rig.step(gx, gy - 60.0f, false);
    CHECK(bottom.try_get<UiBox>()->h == doctest::Approx(before + 60.0f));
}
