#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include <cstdio>
#include <string_view>

#include "gui/Gui.hpp"
#include "gui/interaction/InteractDragDrop.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/loader/GuiLoader.hpp"
#include "gui/style/GuiStyleSheet.hpp"
#include "io/File.hpp"

using namespace burnhope;
using namespace burnhope::gui;

namespace {

constexpr const char* kStyles = R"({
  "classes": {
    "root":  { "direction": "column" },
    "field": { "w": 200, "h": 28, "bg": "field", "alpha": 1, "textSize": 14 },
    "tall":  { "w": 200, "h": 120, "bg": "field", "alpha": 1 }
  }
})";

constexpr const char* kScreen = R"({
  "name": "shell", "class": "root",
  "children": [
    { "name": "search", "class": "field", "text": "", "behaviors": ["edit"], "maxLen": 40 }
  ]
})";

} // namespace

TEST_CASE("edit field: a click focuses it and typed text lands in the buffer") {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    ctx.style = &sheet;
    const bool ok = guiStyleLoad(sheet, kStyles) && guiInit(ctx, world) && guiLoadScreen(world, sheet, kScreen, flecs::entity(), &root);
    CHECK(ok);
    if (!ok) {
        return;
    }
    guiLayout(ctx, 400.0f, 300.0f);
    const flecs::entity field = root.lookup("shell::search").is_valid() ? root.lookup("shell::search") : root.lookup("search");
    CHECK(field.is_valid());
    if (!field.is_valid()) {
        guiLayoutDetach(root);
        return;
    }
    const UiBox box = *field.try_get<UiBox>();

    InputFrame press{};
    press.pointer.x = box.x + 10.0f;
    press.pointer.y = box.y + 10.0f;
    press.pointer.down = kPointerLeft;
    press.pointer.pressed = kPointerLeft;
    guiConsumeInput(ctx, press);
    guiLayout(ctx, 400.0f, 300.0f);
    CHECK(ctx.interact.focus == field.id());

    InputFrame typed{};
    typed.pointer.x = press.pointer.x;
    typed.pointer.y = press.pointer.y;
    typed.text[0] = 'a';
    typed.text[1] = 'b';
    typed.textLen = 2;
    guiConsumeInput(ctx, typed);
    guiLayout(ctx, 400.0f, 300.0f);
    const UiText* t = field.try_get<UiText>();
    CHECK(t != nullptr);
    if (t != nullptr) {
        CHECK(std::string_view(t->text, t->len) == "ab");
    }
    guiLayoutDetach(root);
}

TEST_CASE("shell layout: every point of the docks is owned by the GUI, the viewport is not") {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    ctx.style = &sheet;
    const bool ok = guiStyleLoadFile(sheet, "assets/ui/styles.json") && guiInit(ctx, world);
    CHECK(ok);
    if (!ok) {
        return;
    }
    MappedFile file{};
    CHECK(fileMapReadOnly("assets/gui/shell.json", &file));
    if (file.data == nullptr) {
        return;
    }
    const bool loaded = guiLoadScreen(world, sheet, std::string_view{reinterpret_cast<const char*>(file.data), file.size},
                                      flecs::entity(), &root);
    fileUnmap(&file);
    CHECK(loaded);
    if (!loaded) {
        return;
    }
    guiLayout(ctx, 1600.0f, 900.0f);
    const auto owned = [&](float x, float y) {
        InputFrame hover{};
        hover.pointer.x = x;
        hover.pointer.y = y;
        guiConsumeInput(ctx, hover);
        return ctx.interact.under != 0;
    };
    CHECK(owned(1500.0f, 450.0f));  // inspector
    CHECK(owned(800.0f, 800.0f));   // bottom dock, empty part of the file grid
    CHECK(owned(100.0f, 800.0f));   // bottom dock, tree
    CHECK(!owned(400.0f, 300.0f));  // the 3D viewport
    guiLayoutDetach(root);
}

namespace {

struct Screen {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    bool ok = false;
    explicit Screen(std::string_view source) {
        ctx.style = &sheet;
        ok = guiStyleLoad(sheet, kStyles) && guiInit(ctx, world) && guiLoadScreen(world, sheet, source, flecs::entity(), &root);
    }
    ~Screen() {
        if (root.is_valid()) {
            guiLayoutDetach(root);
        }
    }
};

} // namespace

TEST_CASE("loader: $include splices the nodes of another file into the children array") {
    Screen screen(R"({ "name": "shell", "class": "root", "children": [
        { "$include": "tests/gui/IncludeFixture.json" },
        { "name": "tail", "class": "field" } ] })");
    CHECK(screen.ok);
    if (screen.ok) {
        CHECK(screen.root.lookup("included").is_valid());
        CHECK(screen.root.lookup("tail").is_valid());
    }
}

TEST_CASE("loader: an $include of a missing file fails the load") {
    Screen screen(R"({ "name": "shell", "class": "root", "children": [ { "$include": "assets/gui/missing.json" } ] })");
    CHECK(!screen.ok);
}

TEST_CASE("hit test: a passive node never takes the pointer") {
    Screen screen(R"({ "name": "shell", "class": "root", "children": [
        { "name": "under", "class": "field" },
        { "name": "ghost", "class": "field", "passive": true } ] })");
    CHECK(screen.ok);
    if (!screen.ok) {
        return;
    }
    guiLayout(screen.ctx, 400.0f, 300.0f);
    InputFrame hover{};
    hover.pointer.x = 10.0f;
    hover.pointer.y = 10.0f;
    guiConsumeInput(screen.ctx, hover);
    CHECK(screen.ctx.interact.under == screen.root.lookup("under").id());
}

TEST_CASE("scrollbar: a press on the right band drags the thumb and scrolls the content") {
    Screen screen(R"({ "name": "shell", "class": "root", "children": [
        { "name": "list", "class": "tall", "behaviors": ["scroll"] } ] })");
    CHECK(screen.ok);
    if (!screen.ok) {
        return;
    }
    guiLayout(screen.ctx, 400.0f, 300.0f);
    const flecs::entity list = screen.root.lookup("list");
    const UiBox box = *list.try_get<UiBox>();
    UiScroll* scroll = list.try_get_mut<UiScroll>();
    CHECK(scroll != nullptr);
    if (scroll == nullptr) {
        return;
    }
    scroll->viewH = box.h;
    scroll->contentH = box.h * 4.0f;
    InputFrame press{};
    press.pointer.x = box.x + box.w - 3.0f;
    press.pointer.y = box.y + 3.0f;
    press.pointer.down = kPointerLeft;
    press.pointer.pressed = kPointerLeft;
    guiConsumeInput(screen.ctx, press);
    CHECK(screen.ctx.interact.barDrag == list.id());
    InputFrame drag = press;
    drag.pointer.pressed = 0;
    drag.pointer.y = box.y + box.h - 3.0f;
    guiConsumeInput(screen.ctx, drag);
    CHECK(list.try_get<UiScroll>()->offY > box.h * 2.0f);
    InputFrame release{};
    release.pointer.x = drag.pointer.x;
    release.pointer.y = drag.pointer.y;
    release.pointer.released = kPointerLeft;
    guiConsumeInput(screen.ctx, release);
    CHECK(screen.ctx.interact.barDrag == 0);
}

TEST_CASE("shell: clicking the real file-browser search field focuses it and typing fills it") {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    ctx.style = &sheet;
    const bool ok = guiStyleLoadFile(sheet, "assets/ui/styles.json") && guiInit(ctx, world);
    CHECK(ok);
    if (!ok) {
        return;
    }
    MappedFile file{};
    CHECK(fileMapReadOnly("assets/gui/shell.json", &file));
    if (file.data == nullptr) {
        return;
    }
    const bool loaded = guiLoadScreen(world, sheet, std::string_view{reinterpret_cast<const char*>(file.data), file.size},
                                      flecs::entity(), &root);
    fileUnmap(&file);
    CHECK(loaded);
    if (!loaded) {
        return;
    }
    guiLayout(ctx, 1600.0f, 900.0f);
    flecs::entity field = world.lookup("shell::files::bar::search");
    if (!field.is_valid()) {
        world.each([&](flecs::entity e, const UiEditable&) { field = e; });
    }
    CHECK(field.is_valid());
    if (!field.is_valid()) {
        guiLayoutDetach(root);
        return;
    }
    CHECK(field.has<UiFocusable>());
    const UiBox box = *field.try_get<UiBox>();
    InputFrame press{};
    press.pointer.x = box.x + box.w * 0.5f;
    press.pointer.y = box.y + box.h * 0.5f;
    press.pointer.down = kPointerLeft;
    press.pointer.pressed = kPointerLeft;
    guiConsumeInput(ctx, press);
    guiLayout(ctx, 1600.0f, 900.0f);
    CHECK(ctx.interact.focus == field.id());

    InputFrame typed{};
    typed.pointer = press.pointer;
    typed.pointer.pressed = 0;
    typed.pointer.down = 0;
    typed.text[0] = 'm';
    typed.text[1] = 'a';
    typed.textLen = 2;
    guiConsumeInput(ctx, typed);
    guiLayout(ctx, 1600.0f, 900.0f);
    const UiText* t = field.try_get<UiText>();
    CHECK(t != nullptr);
    if (t != nullptr) {
        CHECK(std::string_view(t->text, t->len) == "ma");
    }
    guiLayoutDetach(root);
}

namespace {

struct DropLog {
    uint32_t picks = 0;
    uint32_t drops = 0;
    uint32_t lastKind = 0;
};

bool testPick(void* user, float, float, GuiDragPayload& out) {
    ++static_cast<DropLog*>(user)->picks;
    std::snprintf(out.label, sizeof(out.label), "%s", "materials");
    return true;
}

void testTarget(void*, float x, float, const GuiDragPayload&, GuiDropHit& hit) {
    if (x > 100.0f) {
        hit = GuiDropHit{.x = 100.0f, .y = 10.0f, .w = 50.0f, .h = 20.0f, .kind = 7, .highlight = 1};
    }
}

void testDrop(void* user, float, float, const GuiDragPayload&, const GuiDropHit& hit) {
    auto* log = static_cast<DropLog*>(user);
    ++log->drops;
    log->lastKind = hit.kind;
}

constexpr const char* kDragScreen = R"({
  "name": "shell", "class": "root",
  "children": [
    { "name": "dragGhost", "class": "tall", "passive": true, "text": "", "hidden": true },
    { "name": "dropBox", "class": "tall", "passive": true, "hidden": true }
  ]
})";

} // namespace

TEST_CASE("drag & drop: the deadzone arms the ghost, the target is outlined and a release drops") {
    Screen screen(kDragScreen);
    CHECK(screen.ok);
    if (!screen.ok) {
        return;
    }
    guiLayout(screen.ctx, 400.0f, 300.0f);
    const flecs::entity ghost = screen.world.lookup("shell::dragGhost");
    const flecs::entity box = screen.world.lookup("shell::dropBox");
    CHECK(ghost.is_valid());
    CHECK(box.is_valid());
    if (!ghost.is_valid() || !box.is_valid()) {
        return;
    }
    DropLog log;
    GuiDragDropState state{.ghost = ghost.id(), .box = box.id()};
    const GuiDragDropOps ops{.user = &log, .pick = testPick, .target = testTarget, .drop = testDrop};

    guiDragDropRun(screen.world, state, ops, 20.0f, 20.0f, true, false, 400.0f, 300.0f);
    CHECK(log.picks == 1);
    CHECK(state.phase == GuiDragPhase::Armed);
    guiDragDropRun(screen.world, state, ops, 24.0f, 20.0f, true, false, 400.0f, 300.0f); // 4 px: still inside the deadzone
    CHECK(state.phase == GuiDragPhase::Armed);
    CHECK(ghost.has<UiHidden>());
    guiDragDropRun(screen.world, state, ops, 120.0f, 20.0f, true, false, 400.0f, 300.0f);
    CHECK(state.phase == GuiDragPhase::Live);
    CHECK(!ghost.has<UiHidden>());
    CHECK(!box.has<UiHidden>());
    const UiText* caption = ghost.try_get<UiText>();
    CHECK(caption != nullptr);
    if (caption != nullptr) {
        CHECK(std::string_view(caption->text, caption->len) == "materials");
    }
    guiDragDropRun(screen.world, state, ops, 120.0f, 20.0f, false, false, 400.0f, 300.0f);
    CHECK(log.drops == 1);
    CHECK(log.lastKind == 7);
    CHECK(state.phase == GuiDragPhase::Idle);
    CHECK(ghost.has<UiHidden>());
    CHECK(box.has<UiHidden>());

    guiDragDropRun(screen.world, state, ops, 20.0f, 20.0f, true, false, 400.0f, 300.0f); // a second gesture, cancelled by Escape
    guiDragDropRun(screen.world, state, ops, 200.0f, 20.0f, true, false, 400.0f, 300.0f);
    CHECK(state.phase == GuiDragPhase::Live);
    guiDragDropRun(screen.world, state, ops, 200.0f, 20.0f, true, true, 400.0f, 300.0f);
    CHECK(state.phase == GuiDragPhase::Idle);
    guiDragDropRun(screen.world, state, ops, 200.0f, 20.0f, false, false, 400.0f, 300.0f);
    CHECK(log.drops == 1);
}
