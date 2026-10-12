#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>
#include <yoga/Yoga.h>

#include "ui/Domain.hpp"
#include "ui/State.hpp"
#include "ui/Layout.hpp"
#include "ui/widget/Detail.hpp"

#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

using namespace burnhope;

TEST_CASE("layout dump names the node") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(80, 24), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiName(state, id, "save");
    uiLayout(state, 200.0f, 80.0f);
    uiTrace(state, "test", id);
    uiDumpLayout(state);
    CHECK(state.box[id].w > 1.0f);
    CHECK(std::strcmp(state.ent[id].try_get<UiName>()->bytes, "save") == 0);
}
TEST_CASE("column label skips yoga when the line changes") {
    UiState state;
    UiFlex col{};
    const uint16_t column = spawn(state, kUiNone, col, paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t id = spawn(state, column, UiFlex{}, paint(UiRole::Label, 1.0f, 1.0f, 1.0f));
    addText(state, id, "fps 1");
    CHECK(state.layoutDirty);
    uiLayout(state, 400.0f, 300.0f);
    CHECK(!state.layoutDirty);
    const float y = state.box[id].y;
    addText(state, id, "fps 1000");
    CHECK(!state.layoutDirty);
    CHECK(state.visualDirty);
    CHECK(state.box[id].w > 20.0f);
    CHECK(state.box[id].y == doctest::Approx(y));
    state.layoutDirty = false;
    state.visualDirty = false;
    addText(state, id, "fps 1000");
    CHECK(!state.layoutDirty);
    CHECK(!state.visualDirty);
}
TEST_CASE("row label still marks layout") {
    UiState state;
    UiFlex row{};
    row.direction = 1;
    const uint16_t line = spawn(state, kUiNone, row, paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t id = spawn(state, line, UiFlex{}, paint(UiRole::Label, 1.0f, 1.0f, 1.0f));
    addText(state, id, "a");
    uiLayout(state, 400.0f, 80.0f);
    addText(state, id, "abcd");
    CHECK(state.layoutDirty);
}
TEST_CASE("yoga box") {
    UiState state;
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = 120.0f;
    flex.height = 40.0f;
    const uint16_t root = spawn(state, kUiNone, flex, paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex childFlex{};
    childFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    childFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    childFlex.width = 120.0f;
    childFlex.height = 40.0f;
    const uint16_t child = spawn(state, root, childFlex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiLayout(state, 400.0f, 300.0f);
    CHECK(state.box[root].w == doctest::Approx(400.0f));
    CHECK(state.box[child].w == doctest::Approx(120.0f));
    CHECK(state.box[child].h == doctest::Approx(40.0f));
    CHECK(state.ent[child].try_get<UiBounds>() != nullptr);
}
TEST_CASE("tab index wraps") {
    UiState state;
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = 80.0f;
    flex.height = 24.0f;
    const uint16_t a = spawn(state, kUiNone, flex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    const uint16_t b = spawn(state, kUiNone, flex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiTab(state, a, 2);
    uiTab(state, b, 1);
    uiLayout(state, 200.0f, 200.0f);
    uiFocus(state, a);
    uiFocusStep(state, 1);
    CHECK(state.focused == b);
    uiFocusStep(state, 1);
    CHECK(state.focused == a);
}
TEST_CASE("scroll scissor blocks the child") {
    UiState state;
    UiFlex host{};
    host.widthMode = static_cast<uint8_t>(UiSize::Px);
    host.heightMode = static_cast<uint8_t>(UiSize::Px);
    host.width = 200.0f;
    host.height = 200.0f;
    const uint16_t root = spawn(state, kUiNone, host, paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    UiFlex scrollFlex{};
    scrollFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    scrollFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    scrollFlex.width = 100.0f;
    scrollFlex.height = 40.0f;
    scrollFlex.overflow = 2;
    const uint16_t scroll = spawn(state, root, scrollFlex, paint(UiRole::Scroll, 0.1f, 0.1f, 0.1f));
    UiFlex child{};
    child.position = 1;
    child.posY = 80.0f;
    child.widthMode = static_cast<uint8_t>(UiSize::Px);
    child.heightMode = static_cast<uint8_t>(UiSize::Px);
    child.width = 40.0f;
    child.height = 20.0f;
    const uint16_t button = spawn(state, scroll, child, paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    uiLayout(state, 200.0f, 200.0f);
    const uint16_t hit = hitTest(state, state.box[button].x + 2.0f, state.box[button].y + 2.0f);
    CHECK(hit != button);
}
TEST_CASE("drop frees the subtree") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(200, 80), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t child = spawn(state, root, px(40, 20), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    CHECK(state.ent[child].try_get<UiText>() != nullptr);
    uiLayout(state, 200.0f, 80.0f);
    CHECK(state.yoga[child] != nullptr);
    uiDrop(state, child);
    CHECK(state.yoga[child] == nullptr);
    CHECK(!state.ent[child].is_alive());
    CHECK(state.ent[root].is_alive());
    CHECK(YGNodeGetChildCount(state.yoga[root]) == 0);
    uiLayout(state, 200.0f, 80.0f);
    CHECK(hitTest(state, 10.0f, 10.0f) != child);
}
TEST_CASE("reparent moves the yoga parent") {
    UiState state;
    const uint16_t host = spawn(state, kUiNone, px(220, 40), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    const uint16_t a = spawn(state, host, px(100, 40), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t b = spawn(state, host, px(100, 40), paint(UiRole::Panel, 0.2f, 0.2f, 0.2f));
    const uint16_t child = spawn(state, a, px(20, 10), paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    uiReparent(state, child, b);
    CHECK(state.parentOf[child] == b);
    CHECK(YGNodeGetParent(state.yoga[child]) == state.yoga[b]);
    uiLayout(state, 220.0f, 40.0f);
    CHECK(state.box[child].w == doctest::Approx(20.0f));
}
TEST_CASE("hidden node is not a hit") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(80, 30), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiShow(state, id, false);
    uiLayout(state, 200.0f, 80.0f);
    CHECK(YGNodeStyleGetDisplay(state.yoga[id]) == YGDisplayNone);
    CHECK(hitTest(state, 10.0f, 10.0f) == kUiNone);
}
TEST_CASE("scissor edge and a nested child") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(200, 80), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    UiFlex scrollFlex = px(100, 40);
    scrollFlex.overflow = 2;
    const uint16_t scroll = spawn(state, root, scrollFlex, paint(UiRole::Scroll, 0.1f, 0.1f, 0.1f));
    UiFlex deep = px(80, 80);
    deep.position = 1;
    deep.posY = 0.0f;
    deep.overflow = 2;
    const uint16_t inner = spawn(state, scroll, deep, paint(UiRole::Scroll, 0.05f, 0.05f, 0.05f));
    UiFlex buried = px(16, 16);
    buried.position = 1;
    buried.posY = 50.0f;
    const uint16_t buriedBtn = spawn(state, inner, buried, paint(UiRole::Button, 0.4f, 0.2f, 0.2f));
    UiFlex child = px(30, 16);
    child.position = 1;
    child.posX = 10.0f;
    child.posY = 8.0f;
    const uint16_t button = spawn(state, scroll, child, paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    uiLayout(state, 200.0f, 80.0f);
    const UiBox& box = state.box[button];
    CHECK(hitTest(state, box.x + 1.0f, box.y + 1.0f) == button);
    CHECK(hitTest(state, box.x - 1.0f, box.y + 1.0f) != button);
    CHECK(hitTest(state, box.x + box.w, box.y + 1.0f) != button);
    CHECK(hitTest(state, box.x + box.w - 1.0f, box.y + 1.0f) == button);
    const UiBox& deepBox = state.box[buriedBtn];
    CHECK(deepBox.h > 1.0f);
    CHECK(hitTest(state, deepBox.x + 1.0f, deepBox.y + 1.0f) != buriedBtn);
}
TEST_CASE("uiLayout timing on a near-cap tree") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(1600, 1000), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex grid{};
    grid.direction = 1; // row
    grid.wrap = 1;
    grid.widthMode = static_cast<uint8_t>(UiSize::Px);
    grid.heightMode = static_cast<uint8_t>(UiSize::Px);
    grid.width = 40.0f;
    grid.height = 24.0f;
    grid.shrink = 0.0f;
    uint16_t made = 0;
    for (uint16_t i = 0; i < 900 && state.count < kUiCap - 4; ++i) {
        const uint16_t id = spawn(state, root, grid, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
        if (id == kUiNone) {
            break;
        }
        ++made;
    }
    const auto t0 = std::chrono::steady_clock::now();
    uiLayout(state, 1600.0f, 1000.0f);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    spdlog::info("bench uiLayout nodes={} ms={:.3f}", static_cast<unsigned>(made), ms);
    CHECK(made > 500);
    CHECK(state.box[root].w == doctest::Approx(1600.0f));
}
TEST_CASE("uiEmit timing on a near-cap tree") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(1600, 1000), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex grid{};
    grid.direction = 1;
    grid.wrap = 1;
    grid.widthMode = static_cast<uint8_t>(UiSize::Px);
    grid.heightMode = static_cast<uint8_t>(UiSize::Px);
    grid.width = 40.0f;
    grid.height = 24.0f;
    grid.shrink = 0.0f;
    uint16_t made = 0;
    for (uint16_t i = 0; i < 900 && state.count < kUiCap - 4; ++i) {
        const uint16_t id = spawn(state, root, grid, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
        if (id == kUiNone) {
            break;
        }
        ++made;
    }
    uiLayout(state, 1600.0f, 1000.0f);
    std::vector<UiPrimitive> prims(kUiPrimCap);
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t n = uiEmit(state, prims.data(), kUiPrimCap);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    spdlog::info("bench uiEmit nodes={} prims={} ms={:.3f}", static_cast<unsigned>(made), n, ms);
    CHECK(n > 0);
}
