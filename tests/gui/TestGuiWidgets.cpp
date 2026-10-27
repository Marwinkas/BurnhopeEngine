#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "gui/Gui.hpp"
#include "gui/input/GuiHitTest.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/loader/GuiLoader.hpp"
#include "gui/render/GuiEmitter.hpp"
#include "gui/style/GuiStyleSheet.hpp"

using namespace burnhope;
using namespace burnhope::gui;

namespace {

constexpr const char* kStyles = R"({
  "classes": {
    "root":   { "direction": "column" },
    "bar":    { "h": 30, "direction": "row", "bg": "title", "alpha": 1 },
    "button": { "w": 60, "h": 30, "bg": "button", "textSize": 20, "textAlign": "center" },
    "body":   { "w": 100, "h": 50, "bg": "content", "alpha": 1 },
    "menu":   { "w": 80, "h": 60, "bg": "menu", "alpha": 1 },
    "track":  { "w": 100, "h": 20, "bg": "slider", "alpha": 1 },
    "list":   { "w": 120, "h": 60, "shrink": 0 },
    "item":   { "textSize": 12 }
  }
})";

constexpr const char* kScreen = R"({
  "name": "shell", "class": "root",
  "children": [
    { "name": "bar", "class": "bar", "children": [
      { "name": "fold", "class": "button", "text": "Fold", "behaviors": ["hover", "click"], "action": 7, "toggles": "body" },
      { "name": "menuBtn", "class": "button", "text": "Menu", "behaviors": ["hover", "click"], "action": 8, "toggles": "menu" }
    ] },
    { "name": "body", "class": "body" },
    { "name": "menu", "class": "menu", "behaviors": ["overlay"], "placement": "below" },
    { "name": "track", "class": "track", "behaviors": ["hover", "drag"], "axis": "x", "value": 0.5 },
    { "name": "flat", "class": "track", "behaviors": ["hover", "drag"], "axis": "x", "value": 0.5, "flat": true }
  ]
})";

struct Fixture {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    bool ok = false;
    Fixture() {
        ctx.style = &sheet;
        ok = guiStyleLoad(sheet, kStyles) && guiInit(ctx, world) && guiLoadScreen(world, sheet, kScreen, flecs::entity(), &root);
        CHECK(ok);
        if (ok) {
            guiLayout(ctx, 400.0f, 300.0f);
        }
    }
    ~Fixture() {
        if (root.is_valid()) {
            guiLayoutDetach(root);
        }
    }
    void click(float x, float y) {
        InputFrame press{};
        press.pointer.x = x;
        press.pointer.y = y;
        press.pointer.down = kPointerLeft;
        press.pointer.pressed = kPointerLeft;
        guiConsumeInput(ctx, press);
        guiLayout(ctx, 400.0f, 300.0f);
        InputFrame release{};
        release.pointer.x = x;
        release.pointer.y = y;
        release.pointer.released = kPointerLeft;
        guiConsumeInput(ctx, release);
        guiLayout(ctx, 400.0f, 300.0f);
    }
    void clickOn(flecs::entity e) {
        const UiBox box = *e.try_get<UiBox>();
        click(box.x + box.w * 0.5f, box.y + box.h * 0.5f);
    }
    uint32_t emitted() {
        static UiPrimitive buffer[kUiPrimCap];
        return guiEmit(world, sheet, buffer, kUiPrimCap);
    }
};

} // namespace

TEST_CASE("style sheet: textSize reaches the node text") {
    Fixture f;
    const UiText* text = f.ok ? f.root.lookup("bar::fold").try_get<UiText>() : nullptr;
    if (!(text != nullptr)) {
        FAIL("fold button missing");
        return;
    }
    CHECK(text->size == 20);
    CHECK(text->align == 1);
}

TEST_CASE("toggles: a click shows and hides the target and frees its layout space") {
    Fixture f;
    const flecs::entity body = f.root.lookup("body");
    const flecs::entity track = f.root.lookup("track");
    if (!(f.ok && body.is_valid() && track.is_valid())) {
        FAIL("fixture failed");
        return;
    }
    const float trackY = track.try_get<UiBox>()->y;
    CHECK(!body.has<UiHidden>());

    f.clickOn(f.root.lookup("bar::fold"));
    CHECK(body.has<UiHidden>());
    CHECK(track.try_get<UiBox>()->y == doctest::Approx(trackY - 50.0f));

    f.clickOn(f.root.lookup("bar::fold"));
    CHECK(!body.has<UiHidden>());
    CHECK(track.try_get<UiBox>()->y == doctest::Approx(trackY));
}

TEST_CASE("toggles: a click opens an overlay at the trigger and an overlay child is its own layout root") {
    Fixture f;
    const flecs::entity menu = f.root.lookup("menu");
    const flecs::entity button = f.root.lookup("bar::menuBtn");
    if (!(f.ok && menu.is_valid() && button.is_valid())) {
        FAIL("fixture failed");
        return;
    }
    CHECK(menu.try_get<UiOverlay>()->open == 0);
    CHECK(YGNodeGetParent(menu.try_get<UiFlex>()->node) == nullptr);

    f.clickOn(button);
    const UiOverlay* overlay = menu.try_get<UiOverlay>();
    CHECK(overlay->open == 1);
    const UiBox anchor = *button.try_get<UiBox>();
    CHECK(overlay->anchorX == doctest::Approx(anchor.x));
    CHECK(overlay->anchorH == doctest::Approx(anchor.h));
    CHECK(menu.try_get<UiBox>()->y >= anchor.y + anchor.h - 0.01f);
}

TEST_CASE("hit test: a transparent layout container does not occlude the scene behind it") {
    Fixture f;
    if (!(f.ok)) {
        FAIL("fixture failed");
        return;
    }
    CHECK(guiHitTest(f.world, 390.0f, 290.0f).id() == 0); // only the transparent root is there
    flecs::entity under;
    (void)guiHitTestEx(f.world, 390.0f, 290.0f, &under);
    CHECK(under.id() == 0);
    CHECK(guiHitTest(f.world, 10.0f, 10.0f).id() == f.root.lookup("bar::fold").id());
}

TEST_CASE("emitter: a horizontal draggable paints a value bar unless it is flat") {
    Fixture f;
    if (!(f.ok)) {
        FAIL("fixture failed");
        return;
    }
    const uint32_t withBar = f.emitted();
    f.root.lookup("track").destruct();
    const uint32_t withoutTrack = f.emitted();
    f.root.lookup("flat").destruct();
    const uint32_t withoutFlat = f.emitted();
    CHECK(withBar - withoutTrack == 2); // track panel + its bar
    CHECK(withoutTrack - withoutFlat == 1); // flat panel only
}
