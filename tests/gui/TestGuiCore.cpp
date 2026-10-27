#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "Gui.hpp"
#include <cstring>

#include "layout/GuiLayoutEngine.hpp"
#include "render/GuiEmitter.hpp"
#include "loader/GuiLoader.hpp"
#include "style/GuiStyleSheet.hpp"

using namespace burnhope;
using namespace burnhope::gui;

namespace {

constexpr const char* kStyles = R"({
  "classes": {
    "root":  { "w": 200, "h": 100, "flexDir": "column" },
    "track": { "w": 100, "h": 20, "bg": "button", "radius": 4 },
    "label": { "h": 20, "bg": "accent" },
    "tall":  { "w": 400, "h": 600, "flexDir": "column" },
    "field": { "w": 120, "h": 24, "bg": "button", "shrink": 0 },
    "list":  { "w": 200, "h": 240, "shrink": 0 },
    "clip":  { "w": 100, "h": 50, "flexDir": "column", "shrink": 0 },
    "pad":   { "w": 10, "h": 100, "shrink": 0 },
    "inbox": { "w": 40, "h": 20, "bg": "accent", "shrink": 0 },
    "outbox":{ "w": 33, "h": 17, "bg": "accent", "shrink": 0 },
    "popup": { "w": 80, "h": 60, "bg": "button" }
  }
})";

constexpr const char* kScreen = R"({
  "class": "root",
  "children": [
    { "name": "track", "class": "track", "behaviors": ["hover", "drag"], "axis": "x", "mode": 0 },
    { "name": "caption", "class": "label", "text": "Hello" }
  ]
})";

struct Fixture {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;
    bool ok = false;
    float screenW = 200.0f;
    float screenH = 100.0f;
    explicit Fixture(const char* screen = kScreen, float w = 200.0f, float h = 100.0f) : screenW(w), screenH(h) {
        ctx.style = &sheet;
        ok = guiStyleLoad(sheet, kStyles) && guiInit(ctx, world) &&
             guiLoadScreen(world, sheet, screen, flecs::entity(), &root);
        CHECK(ok);
        if (ok) {
            guiLayout(ctx, screenW, screenH);
        }
    }
    ~Fixture() {
        if (root.is_valid()) {
            guiLayoutDetach(root);
        }
    }
};

} // namespace

TEST_CASE("gui layout writes absolute UiBox through Yoga") {
    Fixture f;
    const flecs::entity track = f.root.lookup("track");
    if (!f.ok || !track.is_valid()) {
        FAIL("fixture failed");
        return;
    }
    const UiBox* box = track.try_get<UiBox>();
    if (box == nullptr) {
        FAIL("track has no UiBox");
        return;
    }
    CHECK(box->w == doctest::Approx(100.0f));
    CHECK(box->h == doctest::Approx(20.0f));
    const UiBox* cap = f.root.lookup("caption").try_get<UiBox>();
    if (cap == nullptr) {
        FAIL("caption has no UiBox");
        return;
    }
    CHECK(cap->y >= box->y + box->h - 0.01f);
}

TEST_CASE("gui drag engine drives a normalized slider value") {
    Fixture f;
    const flecs::entity track = f.root.lookup("track");
    if (!f.ok || !track.is_valid() || track.try_get<UiBox>() == nullptr) {
        FAIL("fixture failed");
        return;
    }
    const UiBox box = *track.try_get<UiBox>();

    InputFrame press{};
    press.pointer.x = box.x + box.w * 0.25f;
    press.pointer.y = box.y + box.h * 0.5f;
    press.pointer.down = kPointerLeft;
    press.pointer.pressed = kPointerLeft;
    guiConsumeInput(f.ctx, press);
    CHECK(track.try_get<UiDraggable>()->value == doctest::Approx(0.25f).epsilon(0.01));

    InputFrame move{};
    move.pointer.x = box.x + box.w * 0.75f;
    move.pointer.y = press.pointer.y;
    move.pointer.dx = box.w * 0.5f;
    move.pointer.down = kPointerLeft;
    guiConsumeInput(f.ctx, move);
    CHECK(track.try_get<UiDraggable>()->value == doctest::Approx(0.75f).epsilon(0.01));

    InputFrame release{};
    release.pointer.x = move.pointer.x;
    release.pointer.y = move.pointer.y;
    release.pointer.released = kPointerLeft;
    guiConsumeInput(f.ctx, release);
    CHECK(track.try_get<UiDraggable>()->value == doctest::Approx(0.75f).epsilon(0.01));
}

TEST_CASE("gui emitter writes primitives into a mapped buffer") {
    Fixture f;
    static UiPrimitive buffer[kUiPrimCap + 1];
    for (UiPrimitive& p : buffer) {
        p = UiPrimitive{};
    }
    guiSetPrimBuffer(f.ctx, buffer + 1, kUiPrimCap);
    guiRender(f.ctx, VK_NULL_HANDLE);
    CHECK(f.ctx.primCount >= 3);
    bool sized = false;
    for (uint32_t i = 0; i < f.ctx.primCount; ++i) {
        sized = sized || (buffer[1 + i].sizeX > 0.0f && buffer[1 + i].sizeY > 0.0f);
    }
    CHECK(sized);
}

namespace {

constexpr const char* kEditScreen = R"({
  "class": "tall",
  "children": [
    { "name": "field", "class": "field", "text": "", "maxLen": 64, "behaviors": ["edit"] }
  ]
})";

constexpr const char* kListScreen = R"({
  "class": "tall",
  "children": [
    { "name": "list", "class": "list", "behaviors": ["virtual", "hover"], "count": 10000, "itemH": 24, "columns": 1 }
  ]
})";

constexpr const char* kClipScreen = R"({
  "class": "tall",
  "children": [
    { "name": "clip", "class": "clip", "behaviors": ["scroll"], "children": [
      { "name": "inside", "class": "inbox" },
      { "name": "gap", "class": "pad" },
      { "name": "outside", "class": "outbox" }
    ] }
  ]
})";

constexpr const char* kPopupScreen = R"({
  "class": "popup", "name": "popup", "behaviors": ["overlay"], "placement": 0, "dismiss": true
})";

void typeFrame(Fixture& f, const char* text) {
    InputFrame in{};
    in.textLen = static_cast<uint8_t>(std::strlen(text));
    std::memcpy(in.text, text, in.textLen);
    guiConsumeInput(f.ctx, in);
}

uint32_t liveItems(flecs::entity host) {
    uint32_t n = 0;
    host.children([&](flecs::entity c) {
        if (c.has<UiVirtualItem>() && !c.has<UiHidden>()) {
            ++n;
        }
    });
    return n;
}

uint32_t allItems(flecs::entity host) {
    uint32_t n = 0;
    host.children([&](flecs::entity c) {
        if (c.has<UiVirtualItem>()) {
            ++n;
        }
    });
    return n;
}

struct BindLog {
    uint32_t calls = 0;
};

void countBind(flecs::entity, uint32_t, void* user) {
    ++static_cast<BindLog*>(user)->calls;
}

} // namespace

TEST_CASE("gui text edit types moves the caret and deletes") {
    Fixture f(kEditScreen, 400.0f, 600.0f);
    const flecs::entity field = f.root.lookup("field");
    if (!f.ok || !field.is_valid()) {
        FAIL("fixture failed");
        return;
    }
    f.ctx.interact.focus = field.id();

    typeFrame(f, "abc");
    CHECK(std::strcmp(field.try_get<UiText>()->text, "abc") == 0);
    CHECK(field.try_get<UiEditable>()->caret == 3);

    InputFrame left{};
    left.left = 1;
    guiConsumeInput(f.ctx, left);
    CHECK(field.try_get<UiEditable>()->caret == 2);

    InputFrame back{};
    back.backspace = 1;
    guiConsumeInput(f.ctx, back);
    CHECK(std::strcmp(field.try_get<UiText>()->text, "ac") == 0);
    CHECK(field.try_get<UiText>()->len == 2);
    CHECK(field.try_get<UiEditable>()->caret == 1);

    InputFrame right{};
    right.right = 1;
    guiConsumeInput(f.ctx, right);
    CHECK(field.try_get<UiEditable>()->caret == 2);

    typeFrame(f, "Z");
    CHECK(std::strcmp(field.try_get<UiText>()->text, "acZ") == 0);

    InputFrame del{};
    del.left = 1;
    guiConsumeInput(f.ctx, del);
    del = InputFrame{};
    del.del = 1;
    guiConsumeInput(f.ctx, del);
    CHECK(std::strcmp(field.try_get<UiText>()->text, "ac") == 0);

    InputFrame undo{};
    undo.undo = 1;
    guiConsumeInput(f.ctx, undo);
    CHECK(std::strcmp(field.try_get<UiText>()->text, "acZ") == 0);

    typeFrame(f, "0123456789012345678901234567890123456789");
    typeFrame(f, "0123456789012345678901234567890123456789");
    CHECK(field.try_get<UiText>()->len <= 64);
    CHECK(field.try_get<UiEditable>()->caret <= field.try_get<UiText>()->len);
}

TEST_CASE("gui virtual pool recycles a bounded entity set over 10000 items") {
    Fixture f(kListScreen, 400.0f, 600.0f);
    const flecs::entity list = f.root.lookup("list");
    if (!f.ok || !list.is_valid()) {
        FAIL("fixture failed");
        return;
    }
    BindLog log;
    f.ctx.virtualBind = countBind;
    f.ctx.virtualUser = &log;
    guiLayout(f.ctx, 400.0f, 600.0f);

    const UiScroll* sc = list.try_get<UiScroll>();
    if (sc == nullptr) {
        FAIL("list has no UiScroll");
        return;
    }
    CHECK(sc->contentH == doctest::Approx(240000.0f));
    CHECK(allItems(list) <= kGuiVirtualCap);
    CHECK(liveItems(list) >= 10);

    // Tag the item shown in slot 0 (index 0), then scroll 64 rows so that slot shows index 64.
    flecs::entity first;
    list.children([&](flecs::entity c) {
        const UiVirtualItem* it = c.try_get<UiVirtualItem>();
        if (it != nullptr && it->index == 0) {
            first = c;
        }
    });
    if (!first.is_valid()) {
        FAIL("no item bound to index 0");
        return;
    }
    first.add<UiHovered>();
    first.add<UiFocused>();

    list.try_get_mut<UiScroll>()->offY = 64.0f * 24.0f;
    guiLayout(f.ctx, 400.0f, 600.0f);
    CHECK(list.try_get<UiVirtual>()->first == 64);
    CHECK(log.calls >= 10); // every visible slot moved to a new index and was rebound
    CHECK(allItems(list) <= kGuiVirtualCap);
    CHECK(first.try_get<UiVirtualItem>()->index == 64);
    CHECK(!first.has<UiHovered>());
    CHECK(!first.has<UiFocused>());

    list.try_get_mut<UiScroll>()->offY = 9000.0f * 24.0f;
    guiLayout(f.ctx, 400.0f, 600.0f);
    CHECK(allItems(list) <= kGuiVirtualCap);
    CHECK(list.try_get<UiVirtual>()->first >= 9000);
}

TEST_CASE("gui scissor lets a node inherit its parent clip and the emitter skips boxes outside it") {
    Fixture f(kClipScreen, 400.0f, 600.0f);
    const flecs::entity clip = f.root.lookup("clip");
    const flecs::entity inside = clip.lookup("inside");
    const flecs::entity outside = clip.lookup("outside");
    if (!f.ok || !inside.is_valid() || !outside.is_valid()) {
        FAIL("fixture failed");
        return;
    }
    const UiClip* in = inside.try_get<UiClip>();
    const UiClip* out = outside.try_get<UiClip>();
    if (in == nullptr || out == nullptr) {
        FAIL("scissor pass wrote no UiClip");
        return;
    }
    const UiBox* clipBox = clip.try_get<UiBox>();
    if (clipBox == nullptr) {
        FAIL("clip node has no box");
        return;
    }
    // Children keep the clip of their clipping ancestor; it is never narrowed to their own box.
    for (const UiClip* c : {in, out}) {
        CHECK(c->x == doctest::Approx(clipBox->x));
        CHECK(c->y == doctest::Approx(clipBox->y));
        CHECK(c->w == doctest::Approx(clipBox->w));
        CHECK(c->h == doctest::Approx(clipBox->h));
    }

    static UiPrimitive buffer[kUiPrimCap];
    const uint32_t n = guiEmit(f.world, f.sheet, buffer, kUiPrimCap);
    bool sawInside = false;
    bool sawOutside = false;
    for (uint32_t i = 0; i < n; ++i) {
        sawInside = sawInside || (buffer[i].sizeX == 40.0f && buffer[i].sizeY == 20.0f);
        sawOutside = sawOutside || (buffer[i].sizeX == 33.0f && buffer[i].sizeY == 17.0f);
    }
    CHECK(sawInside);
    CHECK(!sawOutside);
}

TEST_CASE("gui overlay closes on an outside click and stays open on an inside click") {
    Fixture f(kPopupScreen, 200.0f, 200.0f);
    const flecs::entity popup = f.root;
    if (!f.ok || !popup.is_valid() || popup.try_get<UiOverlay>() == nullptr) {
        FAIL("fixture failed");
        return;
    }
    CHECK(popup.has<UiHidden>());

    UiOverlay* o = popup.try_get_mut<UiOverlay>();
    o->anchorX = 10.0f;
    o->anchorY = 10.0f;
    o->anchorW = 30.0f;
    o->anchorH = 20.0f;
    o->open = 1;
    guiLayout(f.ctx, 200.0f, 200.0f);
    CHECK(!popup.has<UiHidden>());
    const UiBox box = *popup.try_get<UiBox>();
    CHECK(box.w == doctest::Approx(80.0f));
    CHECK(box.y >= 30.0f - 0.01f); // placed below the anchor

    InputFrame inside{};
    inside.pointer.x = box.x + 5.0f;
    inside.pointer.y = box.y + 5.0f;
    inside.pointer.down = kPointerLeft;
    inside.pointer.pressed = kPointerLeft;
    guiConsumeInput(f.ctx, inside);
    guiLayout(f.ctx, 200.0f, 200.0f);
    CHECK(popup.try_get<UiOverlay>()->open == 1);
    CHECK(!popup.has<UiHidden>());

    InputFrame outside{};
    outside.pointer.x = 190.0f;
    outside.pointer.y = 190.0f;
    outside.pointer.down = kPointerLeft;
    outside.pointer.pressed = kPointerLeft;
    guiConsumeInput(f.ctx, outside);
    guiLayout(f.ctx, 200.0f, 200.0f);
    CHECK(popup.try_get<UiOverlay>()->open == 0);
    CHECK(popup.has<UiHidden>());
    CHECK(f.ctx.interact.overlayOpen == 0);
}

TEST_CASE("gui text metrics accept a real advance table") {
    GuiTextMetrics m;
    guiTextMetricsDefault(m);
    const float defaultW = guiTextWidth(m, "iiii", 16.0f);

    float advances[kGuiGlyphSlots];
    for (float& a : advances) {
        a = 20.0f; // 20 px per glyph at a 40 px font = 0.5 em
    }
    CHECK(guiTextMetricsSetAdvances(m, advances, kGuiGlyphSlots, 40.0f));
    CHECK(guiTextWidth(m, "iiii", 16.0f) == doctest::Approx(4 * 0.5f * 16.0f));
    CHECK(guiTextWidth(m, "iiii", 16.0f) != doctest::Approx(defaultW));
    CHECK(guiTextCaretFromX(m, "iiii", 16.0f, 8.0f * 2.0f + 1.0f) == 2);
    CHECK(guiTextXFromCaret(m, "iiii", 16.0f, 3) == doctest::Approx(24.0f));

    CHECK(!guiTextMetricsSetAdvances(m, nullptr, 1, 40.0f));
    CHECK(!guiTextMetricsSetAdvances(m, advances, kGuiGlyphSlots + 1, 40.0f));
    CHECK(!guiTextMetricsSetAdvances(m, advances, 1, 0.0f));
}
