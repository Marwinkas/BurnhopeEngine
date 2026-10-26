#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "host/PropBind.hpp"
#include "host/PropImport.hpp"
#include "render/SceneBlock.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Widgets.hpp"

#include <string>
#include <string_view>

using namespace burnhope;

namespace {

bool loadOne(UiState& state, const char* name, std::string_view text) {
    const UiTemplateSource one[] = {{name, text}};
    return uiTemplatesLoad(state, one);
}

struct Shipped {
    UiState state;

    Shipped() {
        uiWidgetsInit(state);
        CHECK(uiStyleLoadAsset(state, "assets/ui/styles.json"));
        CHECK(uiTemplatesLoadAsset(state, "assets/ui/core/_manifest.json"));
    }
};

void markPass(UiState& s, uint16_t id) { s.ent[id].add<UiPass>(); }

} // namespace

TEST_CASE("the button blueprint spawns the node a hand-built button gets from the same class") {
    Shipped rig;
    UiState& state = rig.state;
    const UiTemplateId id = uiTemplateFind(state, "core/button");
    CHECK(id != kUiTemplateNone);
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 400.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t button = uiSpawnTemplate(state, root, id);
    CHECK(button != kUiNone);
    const UiFlex* flex = state.ent[button].try_get<UiFlex>();
    CHECK(flex != nullptr);
    if (flex == nullptr) {
        return;
    }
    CHECK(flex->align == UiAlign::Center);
    CHECK(flex->justify == UiJustify::Center);
    CHECK(flex->heightMode == UiSize::Px);
    CHECK(flex->height == uiStyle(state, "btn_default", UiStyleKey::Height));
    CHECK(flex->padL == uiStyle(state, "btn_default", UiStyleKey::PadL));
    CHECK(flex->padR == uiStyle(state, "btn_default", UiStyleKey::PadR));
    CHECK(flex->grow == 0.0f);
    CHECK(flex->shrink == 0.0f);
    CHECK(state.ent[button].has<UiHoverable>());
    CHECK(state.ent[button].has<UiFocusable>());
    CHECK(!state.ent[button].has<UiTextInput>());
    const UiPaint* paint = state.ent[button].try_get<UiPaint>();
    CHECK(paint != nullptr);
    if (paint != nullptr) {
        CHECK(paint->radius == uiStyle(state, "btn_default", UiStyleKey::Radius));
    }
    uiText(state, button, "Go");
    const UiText* text = state.ent[button].try_get<UiText>();
    CHECK(text != nullptr);
    if (text != nullptr) {
        CHECK(text->len == 2);
        CHECK(text->align == UiTextAlign::Center);
        CHECK(text->valign == UiTextValign::Center);
        CHECK(text->ellipsis == UiTextEllipsis::End);
    }
    uiLayout(state, 400.0f, 400.0f);
    CHECK(uiNodeBox(state, button).h == uiStyle(state, "btn_default", UiStyleKey::Height));
}

TEST_CASE("the fold blueprints spawn a header and a clip holding the body, and name the body as a slot") {
    Shipped rig;
    UiState& state = rig.state;
    const UiTemplateId clipId = uiTemplateFind(state, "core/fold_clip");
    CHECK(clipId != kUiTemplateNone);
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 400.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t header = uiSpawnTemplate(state, root, "core/fold_header");
    const uint16_t clip = uiSpawnTemplate(state, root, clipId);
    CHECK(uiNodeChildCount(state, root) == 2);
    CHECK(uiNodeChildCount(state, clip) == 1);
    const uint16_t body = uiTemplateSlot(state, clipId, clip, "body");
    CHECK(body == uiNodeChild(state, clip, 0));
    CHECK(uiTemplateSlot(state, clipId, clip, "nope") == kUiNone);
    const UiFlex* head = state.ent[header].try_get<UiFlex>();
    CHECK(head != nullptr);
    if (head != nullptr) {
        CHECK(head->height == uiStyle(state, "fold_header", UiStyleKey::Height));
        CHECK(head->padL == uiStyle(state, "fold_header", UiStyleKey::PadL));
    }
    const UiPaint* bar = state.ent[header].try_get<UiPaint>();
    CHECK(bar != nullptr);
    if (bar != nullptr) {
        CHECK(bar->iconLeft == static_cast<uint8_t>(uiStyle(state, "fold_header", UiStyleKey::IconLeft)));
        CHECK(bar->iconSide == static_cast<uint8_t>(uiStyle(state, "fold_header", UiStyleKey::Icon)));
    }
}

TEST_CASE("a blueprint may not carry placement keys, unknown names or bad structure, and a failed load keeps the registry") {
    Shipped rig;
    UiState& state = rig.state;
    const char* bad[] = {
        R"({"flex":{"grow":1}})",
        R"({"flex":{"shrink":"no"}})",
        R"({"flex":{"w":10}})",
        R"({"flex":{"self":"center"}})",
        R"({"flex":{"dir":"diagonal"}})",
        R"({"flex":{"wrap":1}})",
        R"({"text":{"align":"sideways"}})",
        R"({"class":"no_such_class"})",
        R"({"traits":["no_such_trait"]})",
        R"({"parts":[{"name":"a"}],"slots":{"x":"b"}})",
        R"({"parts":[{"slots":{}}]})",
        R"({"parts":{"name":"a"}})",
        R"({"parts":[{"name":"x","use":"core/button"}]})",
        R"({"parts":[{"use":"core/missing"}]})",
        R"({"count":2})",
        R"({"unknown":1})",
        R"([])",
        R"({"flex":{"gap":1})",
    };
    for (const char* doc : bad) {
        CHECK_MESSAGE(!loadOne(state, "core/bad", doc), doc);
        CHECK(uiTemplateFind(state, "core/button") != kUiTemplateNone);
        CHECK(uiTemplateFind(state, "core/bad") == kUiTemplateNone);
    }
    const UiTemplateSource twice[] = {{"core/a", "{}"}, {"core/a", "{}"}};
    CHECK(!uiTemplatesLoad(state, twice));
    CHECK(uiTemplateFind(state, "core/button") != kUiTemplateNone);
    std::string deep = "{}";
    for (int i = 0; i < kUiTplDepth + 1; ++i) {
        deep = "{\"parts\":[" + deep + "]}";
    }
    CHECK(!loadOne(state, "core/deep", deep));
    CHECK(uiTemplateFind(state, "core/button") != kUiTemplateNone);
}

TEST_CASE("a part may use an earlier blueprint and repeat, and a part below the root may place itself") {
    Shipped rig;
    UiState& state = rig.state;
    const UiTemplateSource docs[] = {
        {"core/one", R"({"class":"btn_default","flex":{"shrink":0}})"},
        {"core/row3", R"({"flex":{"dir":"row"},"parts":[{"use":"core/one","name":"cell","count":3,"flex":{"grow":1,"self":"stretch","wp":50}}]})"},
    };
    CHECK(uiTemplatesLoad(state, docs));
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 100.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t row = uiSpawnTemplate(state, root, "core/row3");
    CHECK(uiNodeChildCount(state, row) == 3);
    const UiFlex* cell = state.ent[uiNodeChild(state, row, 1)].try_get<UiFlex>();
    CHECK(cell != nullptr);
    if (cell != nullptr) {
        CHECK(cell->grow == 1.0f);
        CHECK(cell->self == UiAlignSelf::Stretch);
        CHECK(cell->widthMode == UiSize::Percent);
        CHECK(cell->height == uiStyle(state, "btn_default", UiStyleKey::Height));
    }
}

TEST_CASE("a spawn-time class overrides the keys it sets and keeps the blueprint's others") {
    Shipped rig;
    UiState& state = rig.state;
    CHECK(loadOne(state, "core/box", R"({"class":"btn_default","flex":{"gap":5}})"));
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 400.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t plain = uiSpawnTemplate(state, root, "core/box");
    const uint16_t ghost = uiSpawnTemplate(state, root, "core/box", "ghost_button");
    CHECK(state.ent[plain].try_get<UiFlex>()->height == uiStyle(state, "btn_default", UiStyleKey::Height));
    CHECK(state.ent[ghost].try_get<UiFlex>()->height == uiStyle(state, "ghost_button", UiStyleKey::Height));
    CHECK(state.ent[ghost].try_get<UiFlex>()->gap == 5.0f);
    CHECK(uiSpawnTemplate(state, root, "core/box", "no_such_class") == kUiNone);
    CHECK(uiSpawnTemplate(state, root, "core/missing") == kUiNone);
}

TEST_CASE("a blueprint reads the style sheet as it is when it spawns") {
    Shipped rig;
    UiState& state = rig.state;
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 400.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t before = uiSpawnTemplate(state, root, "core/button");
    CHECK(state.ent[before].try_get<UiFlex>()->height != 40.0f);
    CHECK(uiStyleLoadOver(state, R"({"classes":{"btn_default":{"h":40}}})"));
    const uint16_t after = uiSpawnTemplate(state, root, "core/button");
    CHECK(state.ent[after].try_get<UiFlex>()->height == 40.0f);
}

TEST_CASE("uiApplyTemplate re-skins an existing node and keeps its label") {
    Shipped rig;
    UiState& state = rig.state;
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 400.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t node = spawn(state, root, px(10.0f, 10.0f), paintRgb(0.5f, 0.5f, 0.5f));
    uiText(state, node, "Item");
    CHECK(uiApplyTemplate(state, node, uiTemplateFind(state, "core/menu_item")));
    const UiFlex* flex = state.ent[node].try_get<UiFlex>();
    CHECK(flex != nullptr);
    if (flex != nullptr) {
        CHECK(flex->padL == uiStyle(state, "menu_item", UiStyleKey::PadL));
    }
    const UiText* text = state.ent[node].try_get<UiText>();
    CHECK(text != nullptr);
    if (text != nullptr) {
        CHECK(text->len == 4);
        CHECK(text->align == UiTextAlign::Left);
    }
    CHECK(state.ent[node].has<UiHoverable>());
    CHECK(!uiApplyTemplate(state, node, uiTemplateFind(state, "core/menu_item"), "no_such_class"));
}

TEST_CASE("traits: widgets register their own, clip and scroll set the overflow") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiTemplateTrait(state, "marked", markPass));
    CHECK(loadOne(state, "core/t", R"({"traits":["marked","text_input","scroll"]})"));
    const uint16_t root = spawn(state, kUiNone, px(100.0f, 100.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t id = uiSpawnTemplate(state, root, "core/t");
    CHECK(state.ent[id].has<UiPass>());
    CHECK(state.ent[id].has<UiTextInput>());
    CHECK(state.ent[id].try_get<UiFlex>()->overflow == UiOverflow::Scroll);
    CHECK(loadOne(state, "core/c", R"({"traits":["clip"]})"));
    CHECK(state.ent[uiSpawnTemplate(state, root, "core/c")].try_get<UiFlex>()->overflow == UiOverflow::Hidden);
}

TEST_CASE("a blueprint that would pass kUiCap spawns nothing") {
    Shipped rig;
    UiState& state = rig.state;
    const uint16_t root = spawn(state, kUiNone, px(100.0f, 100.0f), paintRgb(0.1f, 0.1f, 0.1f));
    while (state.count < kUiCap - 1) {
        CHECK(spawn(state, root, px(1.0f, 1.0f), paintClear()) != kUiNone);
    }
    const uint16_t before = state.count;
    CHECK(uiSpawnTemplate(state, root, "core/fold_clip") == kUiNone);
    CHECK(state.count == before);
    CHECK(uiSpawnTemplate(state, root, "core/button") != kUiNone);
}

namespace {

struct ImportRig {
    Shipped shipped;
    SceneBlock block{};
    UiPropBinder binder;
    uint16_t root = kUiNone;
    uint16_t named = kUiNone;

    ImportRig() {
        binder.attach(block.scalars, 256, block.flags, 64);
        root = spawn(shipped.state, kUiNone, px(600.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    }

    bool load(std::string_view text) {
        const PropSlot slots[] = {{"probe", &named}};
        return propImport(shipped.state, root, text, "page", binder, slots, 1, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr);
    }
};

} // namespace

TEST_CASE("a layout node with use spawns the blueprint and only places it") {
    ImportRig rig;
    UiState& state = rig.shipped.state;
    CHECK(rig.load(R"({"page":[{"use":"core/button","label":"Go","name":"probe","grow":1,"w":90}]})"));
    CHECK(rig.named != kUiNone);
    if (rig.named == kUiNone) {
        return;
    }
    const UiFlex* flex = state.ent[rig.named].try_get<UiFlex>();
    CHECK(flex != nullptr);
    if (flex == nullptr) {
        return;
    }
    CHECK(flex->grow == 1.0f);
    CHECK(flex->align == UiAlign::Center);
    CHECK(flex->height == uiStyle(state, "btn_default", UiStyleKey::Height));
    CHECK(state.ent[rig.named].try_get<UiText>()->len == 2);
    uiLayout(state, 600.0f, 600.0f);
    CHECK(uiNodeBox(state, rig.named).w == 90.0f);
}

TEST_CASE("use fails the import for a blueprint that is not loaded") {
    ImportRig rig;
    CHECK(!rig.load(R"({"page":[{"use":"core/nonesuch"}]})"));
    ImportRig second;
    CHECK(!second.load(R"({"page":[{"use":"core/button","class":"no_such_class"}]})"));
}

TEST_CASE("button classes size the node: tool buttons are square and padding-free") {
    Shipped rig;
    UiState& state = rig.state;
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 400.0f), paintRgb(0.1f, 0.1f, 0.1f));
    struct Case {
        const char* cls;
        float side;
    };
    for (const Case c : {Case{"tool_button", 28.0f}, Case{"tool_button_sm", 22.0f}, Case{"tool_button_xs", 18.0f}}) {
        const uint16_t id = uiButton(state, root, {.cls = c.cls, .icon = UiIcon::Reset});
        const UiFlex* flex = state.ent[id].try_get<UiFlex>();
        CHECK_MESSAGE(flex != nullptr, c.cls);
        if (flex == nullptr) {
            continue;
        }
        CHECK_MESSAGE(flex->width == c.side, c.cls, " width");
        CHECK_MESSAGE(flex->height == c.side, c.cls, " height");
        CHECK_MESSAGE(flex->padL == 0.0f, c.cls, " padL");
        CHECK_MESSAGE(flex->padR == 0.0f, c.cls, " padR");
        CHECK_MESSAGE(flex->pad == 0.0f, c.cls, " pad");
        CHECK_MESSAGE(flex->shrink == 0.0f, c.cls, " shrink");
    }
}
