#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/widget/Widgets.hpp"
#include "ui/widget/Button.hpp"
#include "ui/State.hpp"
#include "ui/LayoutBridge.hpp"

using namespace burnhope;

TEST_CASE("hiding an ancestor marks descendants hidden in the visibility cache") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(200, 200), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t mid = spawn(state, root, px(100, 100), paintRgb(0.2f, 0.2f, 0.2f));
    const uint16_t leaf = spawn(state, mid, px(40, 40), paintRgb(0.3f, 0.3f, 0.3f));
    uiLayout(state, 200.0f, 200.0f);
    CHECK(state.vis[leaf].hidden == 0);
    uiShow(state, mid, false);
    uiCacheVisibility(state);
    CHECK(state.vis[mid].hidden != 0);
    CHECK(state.vis[leaf].hidden != 0);
    CHECK(state.vis[root].hidden == 0);
    uiShow(state, mid, true);
    uiCacheVisibility(state);
    CHECK(state.vis[leaf].hidden == 0);
}

TEST_CASE("nested clipping parents intersect in the visibility cache") {
    UiState state;
    uiWidgetsInit(state);
    UiFlex outerFlex = px(120, 60);
    outerFlex.overflow = UiOverflow::Hidden;
    UiFlex innerFlex = px(200, 30);
    innerFlex.overflow = UiOverflow::Hidden;
    const uint16_t root = spawn(state, kUiNone, px(400, 400), paintRgb(0.05f, 0.05f, 0.05f));
    const uint16_t outer = spawn(state, root, outerFlex, paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t inner = spawn(state, outer, innerFlex, paintRgb(0.2f, 0.2f, 0.2f));
    const uint16_t leaf = spawn(state, inner, px(300, 300), paintRgb(0.3f, 0.3f, 0.3f));
    uiLayout(state, 400.0f, 400.0f);
    const UiBox& clip = state.vis[leaf].clip;
    CHECK(state.vis[leaf].clipped != 0);
    CHECK(clip.w <= 120.0f + 0.01f);
    CHECK(clip.h <= 30.0f + 0.01f);
    CHECK(clip.x >= state.box[outer].x - 0.01f);
    CHECK(clip.y >= state.box[outer].y - 0.01f);
    CHECK(hitTest(state, state.box[leaf].x + 1.0f, state.box[leaf].y + 1.0f) != kUiNone);
    CHECK(hitTest(state, state.box[outer].x + 110.0f, state.box[outer].y + 50.0f) != leaf);
}

TEST_CASE("icon button is a single entity with an inset image and no children") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(300, 100), paintRgb(0.1f, 0.1f, 0.1f));
    UiButtonConfig cfg{};
    cfg.label = "Open";
    cfg.iconSlot = 1;
    const uint16_t button = uiButton(state, root, cfg);
    CHECK(button != kUiNone);
    const UiImage* image = state.ent[button].try_get<UiImage>();
    CHECK(image != nullptr);
    CHECK((image != nullptr && image->inset > 0));
    CHECK(uiNodeChildCount(state, button) == 0);
    CHECK(uiButtonIconChild(state, button) == button);
    CHECK(uiNodeChildCount(state, root) == 1);
}

TEST_CASE("input tags follow hover, focus and capture without touching other nodes") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(200, 100), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t a = spawn(state, root, px(50, 50), paintRgb(0.2f, 0.2f, 0.2f));
    const uint16_t b = spawn(state, root, px(50, 50), paintRgb(0.3f, 0.3f, 0.3f));
    const uint16_t c = spawn(state, root, px(50, 50), paintRgb(0.4f, 0.4f, 0.4f));
    uiLayout(state, 200.0f, 100.0f);

    state.hovered = a;
    uiSyncInput(state);
    CHECK(state.ent[a].has<UiHovered>());
    CHECK(!state.ent[b].has<UiHovered>());

    state.hovered = b;
    uiSyncInput(state);
    CHECK(!state.ent[a].has<UiHovered>());
    CHECK(state.ent[b].has<UiHovered>());
    CHECK(!state.ent[c].has<UiHovered>());

    state.focused = c;
    uiSyncInput(state);
    CHECK(state.ent[c].has<UiFocused>());
    CHECK(!state.ent[a].has<UiFocused>());

    uiCapture(state, a);
    CHECK(state.capture == a);
    uiSyncInput(state);
    CHECK(state.ent[a].has<UiCapture>());
    CHECK(state.ent[a].has<UiPressed>());
    CHECK(!state.ent[b].has<UiCapture>());

    uiReleaseCapture(state);
    CHECK(state.capture == kUiNone);
    uiSyncInput(state);
    CHECK(!state.ent[a].has<UiCapture>());
    CHECK(!state.ent[a].has<UiPressed>());
    CHECK(state.ent[b].has<UiHovered>());
    CHECK(state.ent[c].has<UiFocused>());
}

TEST_CASE("popup and fade propagate top-down through the visibility cache") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(200, 200), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t mid = spawn(state, root, px(100, 100), paintRgb(0.2f, 0.2f, 0.2f));
    const uint16_t leaf = spawn(state, mid, px(40, 40), paintRgb(0.3f, 0.3f, 0.3f));
    state.ent[mid].add<UiPopup>();
    state.ent[root].set<UiFade>({0.5f});
    state.ent[mid].set<UiFade>({0.5f});
    uiLayout(state, 200.0f, 200.0f);
    uiCacheVisibility(state);
    CHECK(state.vis[root].popup == 0);
    CHECK(state.vis[mid].popup == 1);
    CHECK(state.vis[leaf].popup == 1);
    CHECK(state.vis[root].fadeA == doctest::Approx(0.5f));
    CHECK(state.vis[leaf].fadeA == doctest::Approx(0.25f));
}

TEST_CASE("focus and click-pass facades toggle their tags") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t id = spawn(state, kUiNone, px(20, 20), paintRgb(0.1f, 0.1f, 0.1f));
    uiSetFocusable(state, id);
    uiSetPass(state, id);
    CHECK(state.ent[id].has<UiFocusable>());
    CHECK(state.ent[id].has<UiPass>());
    uiSetFocusable(state, id, false);
    uiSetPass(state, id, false);
    CHECK_FALSE(state.ent[id].has<UiFocusable>());
    CHECK_FALSE(state.ent[id].has<UiPass>());
}
