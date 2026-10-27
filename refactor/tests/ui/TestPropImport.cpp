#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "host/PropBind.hpp"
#include "host/PropImport.hpp"
#include "render/SceneBlock.hpp"
#include "ui/State.hpp"
#include "ui/widget/Widgets.hpp"

#include <string_view>

using namespace burnhope;

namespace {

struct Rig {
    UiState state;
    SceneBlock block{};
    UiPropBinder binder;
    uint16_t root = kUiNone;
    uint16_t named = kUiNone;

    Rig() {
        uiWidgetsInit(state);
        binder.attach(block.scalars, 256, block.flags, 64);
        root = spawn(state, kUiNone, px(600.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    }

    bool load(std::string_view text, const char* key = "page") {
        const PropSlot slots[] = {{"probe", &named}};
        return propImport(state, root, text, key, binder, slots, 1, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr);
    }
};

} // namespace

TEST_CASE("layout keys may come in any order, including after children") {
    Rig rig;
    CHECK(rig.load(R"({"page":[{"children":[{"role":"label","label":"x"}],"name":"probe","role":"panel","w":123,"h":45}]})"));
    CHECK_MESSAGE(rig.named != kUiNone, "the panel was not remembered");
    if (rig.named == kUiNone) {
        return;
    }
    uiLayout(rig.state, 600.0f, 600.0f);
    CHECK(uiNodeBox(rig.state, rig.named).w == 123.0f);
    CHECK(uiNodeBox(rig.state, rig.named).h == 45.0f);
    CHECK(uiNodeChildCount(rig.state, rig.named) == 1);
}

TEST_CASE("layout accepts dir column and col") {
    Rig rig;
    CHECK(rig.load(R"({"page":[{"role":"panel","dir":"col","w":50,"h":50},{"role":"panel","dir":"column","w":50,"h":50}]})"));
}

TEST_CASE("layout import is strict about roles, keys, classes and types") {
    CHECK(!Rig().load(R"({"page":[{"role":"nonesuch"}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","wdith":3}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","class":"no_such_class"}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","w":"wide"}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","wrap":1}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","dir":"diagonal"}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","paint":"neon"}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","children":{"role":"label"}}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel","children":[{"role":"label","extra":1}]}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"menu","items":[{"label":"a"},{"label":"b"},{"label":"c"},{"label":"d"},{"label":"e"},{"label":"f"},{"label":"g"},{"label":"h"},{"label":"i"}]}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"menu","items":[{"labl":"a"}]}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"slider","showWhen":{"flag":1,"gt":2}}]})"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel"}]})", "other"));
    CHECK(!Rig().load(R"([])"));
    CHECK(!Rig().load(R"({"page":[{"role":"panel"})"));
}

TEST_CASE("layout classes fill fields a node leaves out and lose to fields it sets") {
    Rig rig;
    CHECK(rig.load(R"({"page":[{"role":"panel","name":"probe","class":"vector_lock"}]})"));
    uiLayout(rig.state, 600.0f, 600.0f);
    CHECK(uiNodeBox(rig.state, rig.named).w == 18.0f);
    Rig explicitW;
    CHECK(explicitW.load(R"({"page":[{"role":"panel","name":"probe","w":40,"class":"vector_lock"}]})"));
    uiLayout(explicitW.state, 600.0f, 600.0f);
    CHECK(uiNodeBox(explicitW.state, explicitW.named).w == 40.0f);
}
