#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "host/PropGrid.hpp"
#include "ui/State.hpp"
#include "ui/Hover.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/Theme.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Widgets.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <string>

using namespace burnhope;

namespace {

// Every class and blueprint name a call in core/ or demo/ spells as a string literal.
struct Spelled {
    std::string name;
    bool blueprint = false;
    std::string where;
};

void collectLiterals(const std::string& text, size_t from, size_t to, const std::string& file, bool onlyFirst, std::vector<Spelled>& out) {
    size_t at = from;
    while (at < to) {
        const size_t open = text.find('"', at);
        if (open == std::string::npos || open >= to) {
            return;
        }
        const size_t close = text.find('"', open + 1);
        if (close == std::string::npos || close > to) {
            return;
        }
        const std::string word = text.substr(open + 1, close - open - 1);
        const bool name = !word.empty() && word.find_first_not_of("abcdefghijklmnopqrstuvwxyz_/0123456789") == std::string::npos;
        if (name) {
            out.push_back({word, word.rfind("core/", 0) == 0, file});
        }
        if (onlyFirst) {
            return;
        }
        at = close + 1;
    }
}

std::vector<Spelled> spelledNames() {
    std::vector<Spelled> out;
    for (const char* root : {"core", "demo"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
                continue;
            }
            std::ifstream in(entry.path());
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::string file = entry.path().generic_string();
            // uiStyle(s, "class", ...), uiStyleBg(s, "class"), uiStyleResolve(s, "class", ...): the first literal is the class.
            for (const char* call : {"uiStyle(", "uiStyleBg(", "uiStyleResolve("}) {
                for (size_t at = text.find(call); at != std::string::npos; at = text.find(call, at + 1)) {
                    const size_t end = text.find(')', at);
                    collectLiterals(text, at, end == std::string::npos ? text.size() : end, file, true, out);
                }
            }
            // uiSpawnTemplate(s, parent, "core/x", "class") and uiApplyTemplate / uiTemplateFind(s, "core/x"): every literal.
            for (const char* call : {"uiSpawnTemplate(", "uiTemplateFind(", "uiApplyTemplate("}) {
                for (size_t at = text.find(call); at != std::string::npos; at = text.find(call, at + 1)) {
                    const size_t end = text.find(';', at);
                    collectLiterals(text, at, end == std::string::npos ? text.size() : end, file, false, out);
                }
            }
            // .cls = "class" up to the next comma (a ternary may name two).
            for (size_t at = text.find(".cls = "); at != std::string::npos; at = text.find(".cls = ", at + 1)) {
                const size_t stop = text.find_first_of(",}\n", at);
                collectLiterals(text, at, stop == std::string::npos ? text.size() : stop, file, false, out);
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("styles.json and the blueprints define every class and blueprint the code names") {
    UiState state;
    uiWidgetsInit(state);
    const std::vector<Spelled> spelled = spelledNames();
    CHECK(spelled.size() > 100);
    for (const Spelled& item : spelled) {
        if (item.blueprint) {
            CHECK_MESSAGE(uiTemplateFind(state, item.name.c_str()) != kUiTemplateNone, item.where, " spawns unknown blueprint ", item.name);
        } else {
            CHECK_MESSAGE(uiStyleFind(state, item.name.c_str()) != nullptr, item.where, " names unknown style class ", item.name);
        }
    }
}

TEST_CASE("every class of styles.json parses and the sheet fits its caps") {
    UiState state;
    uiWidgetsInit(state);
    const UiStyleSheet* sheet = state.world.try_get<UiStyleSheet>();
    CHECK(sheet != nullptr);
    if (sheet != nullptr) {
        CHECK(sheet->count > 100);
        CHECK(sheet->count < kUiStyleCap);
    }
}

TEST_CASE("style sheet resolves tokens, extends, and keeps the built-in value for a key it omits") {
    UiState state;
    uiWidgetsInit(state);
    const char* doc = R"({"tokens":{"space":{"md":9},"h":31},
        "classes":{"base":{"h":"$h","gap":"$space.md","border":1},
                   "child":{"extends":"base","gap":3,"alpha":0.5},
                   "prop_row":{"h":50}}})";
    CHECK(uiStyleLoadOver(state, doc));
    CHECK(uiStyle(state, "base", UiStyleKey::Height) == 31.0f);
    CHECK(uiStyle(state, "base", UiStyleKey::Gap) == 9.0f);
    CHECK(uiStyle(state, "child", UiStyleKey::Height) == 31.0f);
    CHECK(uiStyle(state, "child", UiStyleKey::Gap) == 3.0f);
    CHECK(uiStyle(state, "child", UiStyleKey::Alpha) == 0.5f);
    CHECK(uiStyle(state, "prop_row", UiStyleKey::Height) == 50.0f);
    CHECK(uiStyle(state, "prop_label", UiStyleKey::Width) == 135.0f);
    CHECK(uiStyleFind(state, "nope") == nullptr);
    UiStyleClass merged;
    CHECK(uiStyleResolve(state, "prop_row", merged));
    CHECK(merged.value[static_cast<size_t>(UiStyleKey::Height)] == 50.0f);
    CHECK(uiStyleResolve(state, "prop_slot", merged));
    CHECK(merged.value[static_cast<size_t>(UiStyleKey::PadL)] == 12.0f);
    CHECK(!uiStyleResolve(state, "nope", merged));
}

TEST_CASE("a class names palette tones and the metric keys the draw hooks read, and extends and overlays keep them") {
    UiState state;
    uiWidgetsInit(state);
    const char* doc = R"({"classes":{"lined":{"lineH":18,"caretInset":7,"imeAlpha":0.9,"steps":16,"bands":24,"zoomIn":0.85,"fillBg":"accent","todayBg":"accent1","errorBg":"accent2"},
                                     "child":{"extends":"lined","lineH":20,"hintBg":"muted"}}})";
    CHECK(uiStyleLoad(state, doc));
    CHECK(uiStyle(state, "lined", UiStyleKey::LineH) == 18.0f);
    CHECK(uiStyle(state, "lined", UiStyleKey::CaretInset) == 7.0f);
    CHECK(uiStyle(state, "lined", UiStyleKey::ImeAlpha) == 0.9f);
    CHECK(uiStyle(state, "lined", UiStyleKey::Steps) == 16.0f);
    CHECK(uiStyle(state, "lined", UiStyleKey::Bands) == 24.0f);
    CHECK(uiStyle(state, "lined", UiStyleKey::ZoomIn) == 0.85f);
    CHECK(uiStyleTone(state, "lined", UiStyleTone::Fill) == UiToken::Accent);
    CHECK(uiStyleTone(state, "lined", UiStyleTone::Today) == UiToken::Accent1);
    CHECK(uiStyleTone(state, "lined", UiStyleTone::Error) == UiToken::Accent2);
    CHECK(uiStyle(state, "child", UiStyleKey::LineH) == 20.0f);
    CHECK(uiStyle(state, "child", UiStyleKey::Steps) == 16.0f);
    CHECK(uiStyleTone(state, "child", UiStyleTone::Fill) == UiToken::Accent);
    CHECK(uiStyleTone(state, "child", UiStyleTone::Hint) == UiToken::Muted);
    CHECK(uiStyleLoadOver(state, R"({"classes":{"lined":{"lineH":21,"markBg":"text"}}})"));
    CHECK(uiStyle(state, "lined", UiStyleKey::LineH) == 21.0f);
    CHECK(uiStyle(state, "lined", UiStyleKey::Steps) == 16.0f);
    CHECK(uiStyleTone(state, "lined", UiStyleTone::Fill) == UiToken::Accent);
    CHECK(uiStyleTone(state, "lined", UiStyleTone::Mark) == UiToken::Text);
    CHECK(std::strcmp(uiStyleToneName(UiStyleTone::MarkActive), "markActiveBg") == 0);
    CHECK(std::strcmp(uiStyleKeyName(UiStyleKey::Bands), "bands") == 0);
}

TEST_CASE("styles.json carries the draw-hook metrics and the sheet value wins over the number the code used to hold") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiStyleLoadAsset(state, "assets/ui/styles.json"));
    using K = UiStyleKey;
    CHECK(uiStyle(state, "input_field", K::CaretW) == 1.5f);
    CHECK(uiStyle(state, "input_field", K::SelectionInset) == 6.0f);
    CHECK(uiStyle(state, "input_field", K::BlinkRate) == 1.6f);
    CHECK(uiStyle(state, "check_box", K::Width) == 22.0f);
    CHECK(uiStyle(state, "check_box", K::Inner) == 10.0f);
    CHECK(uiStyle(state, "check_box", K::CompactW) == 40.0f);
    CHECK(uiStyle(state, "slider", K::ThumbPad) == 4.0f);
    CHECK(uiStyle(state, "curve", K::Steps) == 16.0f);
    CHECK(uiStyle(state, "gradient", K::Bands) == 24.0f);
    CHECK(uiStyle(state, "image_viewer", K::ZoomIn) == 0.85f);
    CHECK(uiStyle(state, "image_viewer", K::ZoomOut) == 1.18f);
    CHECK(uiStyle(state, "dock_zone", K::Trigger) == 0.28f);
    CHECK(uiStyle(state, "dock_zone", K::HintFrac) == 0.34f);
    CHECK(uiStyle(state, "window_limits", K::MinW) == 220.0f);
    CHECK(uiStyle(state, "window_limits", K::Height) == 180.0f);
    CHECK(uiStyle(state, "drag_mark", K::Border) == 1.0f);
    // Drift: the sheet is the authority, not the literal the widget used to carry.
    CHECK(uiStyle(state, "checkbox", K::PadL) == 36.0f);
    CHECK(uiStyle(state, "search_field", K::Width) == 160.0f);
    CHECK(uiStyle(state, "list", K::RowH) == 28.0f);
    CHECK(uiStyle(state, "color_flat", K::Pad) == 12.0f);
    CHECK(uiStyle(state, "color_flat", K::Gap) == 8.0f);
}

TEST_CASE("style sheet rejects bad documents and leaves the loaded sheet alone") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiStyleLoad(state, R"({"classes":{"a":{"h":11}}})"));
    CHECK(!uiStyleLoad(state, R"({"classes":{"a":{"h":"$missing"}}})"));
    CHECK(!uiStyleLoad(state, R"({"classes":{"a":{"extends":"later"},"later":{}}})"));
    CHECK(!uiStyleLoad(state, R"({"classes":{"a":{},"a":{}}})"));
    CHECK(!uiStyleLoad(state, R"({"tokens":{"x":"y"},"classes":{}})"));
    CHECK(!uiStyleLoad(state, "[]"));
    CHECK(uiStyle(state, "a", UiStyleKey::Height) == 11.0f);
    CHECK(uiStyleLoad(state, "{}"));
    CHECK(uiStyleFind(state, "a") == nullptr);
}

TEST_CASE("style sheet holds kUiStyleCap classes and finds each one") {
    UiState state;
    uiWidgetsInit(state);
    std::string doc = "{\"classes\":{";
    for (int i = 0; i < kUiStyleCap; ++i) {
        char entry[48];
        std::snprintf(entry, sizeof(entry), "%s\"c%d\":{\"h\":%d}", i > 0 ? "," : "", i, i + 1);
        doc += entry;
    }
    std::string over = doc + ",\"extra\":{}}}";
    doc += "}}";
    CHECK(uiStyleLoad(state, doc));
    for (int i = 0; i < kUiStyleCap; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "c%d", i);
        CHECK(uiStyle(state, name, UiStyleKey::Height) == static_cast<float>(i + 1));
    }
    CHECK(!uiStyleLoad(state, over));
}

TEST_CASE("a style sheet value moves the property row") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(600.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t host = uiPropHost(state, root);
    const uint16_t row = uiPropRow(state, host, "Label");
    uiLayout(state, 600.0f, 600.0f);
    CHECK(uiNodeBox(state, uiPropRowOf(state, row)).h == 34.0f);
    CHECK(uiStyleLoadOver(state, R"({"classes":{"prop_row":{"h":50}}})"));
    const uint16_t tall = uiPropRow(state, host, "Tall");
    uiLayout(state, 600.0f, 600.0f);
    CHECK(uiNodeBox(state, uiPropRowOf(state, tall)).h == 50.0f);
}

TEST_CASE("styles.json numbers move the label column and the vector row") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiStyleLoadOver(state, R"({"classes":{"prop_label":{"w":160},"prop_slot":{"padL":20},"vector_cap_x":{"w":16,"h":32}}})"));
    const uint16_t root = spawn(state, kUiNone, px(900.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t host = uiPropHost(state, root);
    const UiPropVec3 vec = uiPropVec3(state, host, "Pos", 1.0f, 2.0f, 3.0f, -10.0f, 10.0f, 0.1f);
    uiLayout(state, 900.0f, 600.0f);
    const uint16_t row = uiPropRowOf(state, uiPropSlotOf(state, vec.row));
    const uint16_t label = uiNodeChild(state, row, 0);
    const uint16_t value = uiNodeChild(state, row, 1);
    CHECK(uiNodeBox(state, label).w == 160.0f);
    CHECK(uiNodeBox(state, value).x == uiNodeBox(state, label).x + 160.0f);
    CHECK(state.ent[value].try_get<UiFlex>()->padL == 20.0f);
    const uint16_t badge = uiNodeChild(state, vec.row, 1);
    CHECK(uiNodeBox(state, badge).w == 16.0f);
    CHECK(uiNodeBox(state, badge).h == 32.0f);
}

TEST_CASE("styles.json numbers size buttons, fields, choices and fold headers") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiStyleLoadOver(state, R"({"classes":{"btn_default":{"h":40,"padL":20,"padR":20},"input_field":{"h":31},"input_box":{"extends":"input_field","padL":0,"padR":0},
        "choice":{"w":200},"fold_header":{"h":33}}})"));
    const uint16_t root = spawn(state, kUiNone, px(600.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t button = uiButton(state, root, {.label = "Go"});
    const uint16_t field = uiField(state, root, "text", 0, false);
    const char* labels[] = {"A", "B"};
    const uint16_t choice = uiChoice(state, root, {.labels = labels, .count = 2});
    [[maybe_unused]] const uint16_t fold = uiFoldBody(state, root, "Fold");
    const uint16_t head = uiNodeChild(state, root, uiNodeChildCount(state, root) - 2u);
    uiLayout(state, 600.0f, 600.0f);
    CHECK(uiNodeBox(state, button).h == 40.0f);
    CHECK(state.ent[button].try_get<UiFlex>()->padL == 20.0f);
    CHECK(uiNodeBox(state, field).h == 31.0f);
    CHECK(uiNodeBox(state, choice).w == 200.0f);
    CHECK(state.ent[head].try_get<UiFlex>()->height == 33.0f);
}

namespace {

// The palettes uiLookInk() and uiLookWarm() held before assets/ui/theme.json took them over.
UiLook referenceInk() {
    UiLook look{};
    const float paper[3]{0.11f, 0.11f, 0.12f};
    const float raised[3]{0.20f, 0.20f, 0.22f};
    const float ink[3]{0.94f, 0.94f, 0.92f};
    const float orange[3]{0.93f, 0.45f, 0.12f};
    const auto set = [](float (&dst)[3], const float (&src)[3]) {
        for (int i = 0; i < 3; ++i) {
            dst[i] = src[i];
        }
    };
    const float window[3]{0.07f, 0.07f, 0.08f};
    const float content[3]{0.14f, 0.14f, 0.15f};
    const float field[3]{0.08f, 0.08f, 0.09f};
    const float menu[3]{0.16f, 0.16f, 0.18f};
    const float muted[3]{0.45f, 0.45f, 0.48f};
    const float slider[3]{0.05f, 0.05f, 0.06f};
    const float dim[3]{0.55f, 0.22f, 0.05f};
    set(look.window, window);
    set(look.title, paper);
    set(look.side, paper);
    set(look.content, content);
    set(look.button, raised);
    set(look.field, field);
    set(look.menu, menu);
    set(look.close, orange);
    set(look.text, ink);
    set(look.muted, muted);
    set(look.item, raised);
    set(look.preview, orange);
    set(look.status, ink);
    set(look.slider, slider);
    set(look.check, raised);
    set(look.chrome, paper);
    set(look.accent[0], orange);
    set(look.accent[1], orange);
    set(look.accent[2], dim);
    look.radius = 3.0f;
    look.radiusSm = 2.0f;
    return look;
}

UiLook referenceWarm() {
    UiLook look{};
    const float window[3]{0.16f, 0.12f, 0.09f};
    const float title[3]{0.22f, 0.16f, 0.11f};
    const float content[3]{0.14f, 0.11f, 0.08f};
    const float button[3]{0.28f, 0.20f, 0.14f};
    const float text[3]{0.96f, 0.93f, 0.86f};
    for (int i = 0; i < 3; ++i) {
        look.window[i] = window[i];
        look.title[i] = title[i];
        look.content[i] = content[i];
        look.button[i] = button[i];
        look.text[i] = text[i];
    }
    return look;
}

bool sameLook(const UiLook& a, const UiLook& b) {
    const auto close = [](float x, float y) { return std::fabs(x - y) < 1e-6f; };
    const float* pa = &a.window[0];
    const float* pb = &b.window[0];
    // window .. chrome (16 triples) and accent[3][3] are contiguous floats at the head of UiLook.
    for (int i = 0; i < 16 * 3 + 9; ++i) {
        if (!close(pa[i], pb[i])) {
            return false;
        }
    }
    return close(a.radius, b.radius) && close(a.radiusSm, b.radiusSm) && close(a.motion, b.motion) && close(a.hover, b.hover);
}

} // namespace

TEST_CASE("assets/ui/theme.json holds the ink and warm palettes the code used to build") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiThemeLoadAsset(state, "assets/ui/theme.json"));
    UiLook look{};
    CHECK(uiThemeLook(state, "ink", look));
    CHECK(sameLook(look, referenceInk()));
    CHECK(uiThemeLook(state, "warm", look));
    CHECK(sameLook(look, referenceWarm()));
    CHECK(uiThemeLook(state, "default", look));
    CHECK(sameLook(look, UiLook{}));
    CHECK(!uiThemeLook(state, "nope", look));
}

TEST_CASE("theme documents are strict and a bad one leaves the loaded palettes alone") {
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiThemeLoad(state, R"({"themes":{"a":{"text":[1,0,0],"radius":5}}})"));
    CHECK(!uiThemeLoad(state, R"({"themes":{"b":{"text":[1,0]}}})"));
    CHECK(!uiThemeLoad(state, R"({"themes":{"b":{"sparkle":1}}})"));
    CHECK(!uiThemeLoad(state, R"({"themes":{"b":{},"b":{}}})"));
    CHECK(!uiThemeLoad(state, R"({"palettes":{}})"));
    CHECK(!uiThemeLoad(state, "[]"));
    UiLook look{};
    CHECK(uiThemeLook(state, "a", look));
    CHECK(look.text[0] == 1.0f);
    CHECK(look.radius == 5.0f);
    CHECK(!uiThemeLook(state, "b", look));
}

TEST_CASE("render-path numbers come from the ui class and the sheet moves them") {
    UiState state;
    uiWidgetsInit(state);
    const UiPaintStyle defaults{};
    CHECK(state.paintStyle.disabledAlpha == defaults.disabledAlpha);
    CHECK(uiStyleLoadAsset(state, "assets/ui/styles.json"));
    CHECK(state.paintStyle.disabledAlpha == doctest::Approx(defaults.disabledAlpha));
    CHECK(state.paintStyle.hoverFallback == doctest::Approx(defaults.hoverFallback));
    CHECK(state.paintStyle.iconFitMin == doctest::Approx(defaults.iconFitMin));
    CHECK(state.paintStyle.mapW == doctest::Approx(defaults.mapW));
    CHECK(state.paintStyle.mapRadius == doctest::Approx(defaults.mapRadius));
    CHECK(uiStyleLoadOver(state, R"({"classes":{"ui":{"disabledAlpha":0.2,"hoverMinAlpha":0.5,"hoverFallback":0.9},"minimap":{"w":100}}})"));
    CHECK(state.paintStyle.disabledAlpha == doctest::Approx(0.2f));
    CHECK(state.paintStyle.mapW == doctest::Approx(100.0f));
    CHECK(state.paintStyle.mapH == doctest::Approx(defaults.mapH));
    const uint16_t node = spawn(state, kUiNone, px(40.0f, 40.0f), paintRgb(0.2f, 0.2f, 0.2f));
    uiHover(state, node);
    state.hovered = node;
    float r = 0.2f;
    float g = 0.2f;
    float b = 0.2f;
    float a = 0.3f;
    uiHoverTint(state, node, r, g, b, a);
    CHECK(a == doctest::Approx(0.9f)); // 0.3 is under hoverMinAlpha 0.5, so it is drawn at hoverFallback
    CHECK(r > 0.2f);
    state.ent[node].try_get_mut<UiPaint>()->disabled = true;
    a = 1.0f;
    uiHoverTint(state, node, r, g, b, a);
    CHECK(a == doctest::Approx(0.2f));
}
