#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/Domain.hpp"
#include "ui/State.hpp"
#include "ui/Layout.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/Canvas.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Virtual.hpp"
#include "text/TextLayout.hpp"
#include "rhi/MeshDraw.hpp"

#include <cstring>

using namespace burnhope;

TEST_CASE("wheel on a scroll does not reach the scene") {
    UiState state;
    const uint16_t host = spawn(state, kUiNone, px(200, 200), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    const uint16_t scroll = spawn(state, host, px(100, 40), paint(UiRole::Scroll, 0.1f, 0.1f, 0.1f));
    const uint16_t inner = spawn(state, scroll, px(80, 200), paint(UiRole::Panel, 0.2f, 0.2f, 0.2f));
    state.ent[scroll].set<UiScroll>({0.0f, inner});
    uiLayout(state, 200.0f, 200.0f);
    InputFrame in{};
    in.wheel = -1.0f;
    in.pointer.x = state.box[scroll].x + 4.0f;
    in.pointer.y = state.box[scroll].y + 4.0f;
    (void)uiApplyInput(state, in);
    CHECK(state.ateWheel == 1);
    Canvas canvas{};
    canvas.state.reset(&state);
    canvasMaskScene(canvas, in);
    canvas.state.release();
    CHECK(in.wheel == 0.0f);
    CHECK(in.pointer.down == 0);
}
TEST_CASE("text button takes the label width") {
    UiState state;
    const uint16_t bar = spawn(state, kUiNone, px(200, 30), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex flex{};
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 22.0f;
    flex.shrink = 0.0f;
    const uint16_t id = spawn(state, bar, flex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    addText(state, id, "FILE");
    uiLayout(state, 200.0f, 80.0f);
    CHECK(state.box[id].w > 20.0f);
    CHECK(state.box[id].h == doctest::Approx(22.0f));
}
TEST_CASE("scroll window stays put") {
    UiState state;
    const uint16_t host = spawn(state, kUiNone, px(200, 200), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    UiFlex scrollFlex = px(100, 40);
    scrollFlex.overflow = 2;
    const uint16_t scroll = spawn(state, host, scrollFlex, paint(UiRole::Scroll, 0.1f, 0.1f, 0.1f));
    const uint16_t inner = spawn(state, scroll, px(80, 200), paint(UiRole::Panel, 0.2f, 0.2f, 0.2f));
    const uint16_t top = spawn(state, inner, px(20, 16), paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    state.ent[scroll].set<UiScroll>({0.0f, inner});
    uiLayout(state, 200.0f, 200.0f);
    const float view = state.box[scroll].h;
    CHECK(view == doctest::Approx(40.0f));
    scrollBy(state, scroll, -4.0f);
    uiLayout(state, 200.0f, 200.0f);
    CHECK(state.box[scroll].h == doctest::Approx(view));
    CHECK(state.ent[scroll].try_get<UiScroll>()->offset > 0.0f);
    scrollBy(state, scroll, 40.0f);
    uiLayout(state, 200.0f, 200.0f);
    CHECK(state.box[scroll].h == doctest::Approx(view));
    CHECK(state.ent[scroll].try_get<UiScroll>()->offset == doctest::Approx(0.0f));
    CHECK(state.box[top].h > 1.0f);
}
TEST_CASE("multiline rows and caret") {
    UiState state;
    state.look.advance = 10.0f;
    state.look.glyphH = 18.0f;
    UiField field{};
    field.multi = 1;
    std::memcpy(field.bytes, "ab\ncd", 5);
    field.len = 5;
    FieldRow rows[8]{};
    const int n = fieldRows(state, field, 100.0f, rows, 8);
    CHECK(n == 2);
    CHECK(rows[0].end == 2);
    CHECK(rows[1].begin == 3);
    CHECK(fieldCaretAt2(state, field, 1.0f, 19.0f, 100.0f) == 3);
    CHECK(fieldOffset(state, field, 0, 2) == doctest::Approx(20.0f));
}
TEST_CASE("soft wrap breaks on width") {
    UiState state;
    state.look.advance = 10.0f;
    state.look.glyphH = 18.0f;
    UiField field{};
    field.multi = 1;
    std::memcpy(field.bytes, "abcd", 4);
    field.len = 4;
    FieldRow rows[8]{};
    const int n = fieldRows(state, field, 25.0f, rows, 8);
    CHECK(n == 2);
    CHECK(rows[0].end == 2);
    CHECK(rows[1].begin == 2);
}
TEST_CASE("field undo restores bytes") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(200, 36), paint(UiRole::Field, 0.1f, 0.1f, 0.1f));
    UiField field{};
    std::memcpy(field.bytes, "hi", 2);
    field.len = 2;
    field.caret = 2;
    field.anchor = 2;
    state.ent[id].set(field);
    uiFocus(state, id);
    fieldRemember(state, id, field);
    fieldInsert(*state.ent[id].try_get_mut<UiField>(), "!", 1);
    CHECK(state.ent[id].try_get<UiField>()->len == 3);
    CHECK(fieldUndo(state));
    CHECK(state.ent[id].try_get<UiField>()->len == 2);
    CHECK(std::strcmp(state.ent[id].try_get<UiField>()->bytes, "hi") == 0);
}
TEST_CASE("disabled node absorbs the hit") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(80, 30), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    state.ent[id].try_get_mut<UiPaint>()->disabled = 1;
    uiLayout(state, 200.0f, 80.0f);
    CHECK(hitTest(state, state.box[id].x + 4.0f, state.box[id].y + 4.0f) == id);
}
TEST_CASE("virtual rows show the first page") {
    UiState state;
    const uint16_t list = uiVirtual(state, kUiNone, 20.0f);
    uiVirtualSource(state, list, 40, nullptr, nullptr, nullptr);
    const UiVirtual* virt = state.ent[list].try_get<UiVirtual>();
    CHECK(virt != nullptr);
    if (virt == nullptr) {
        return;
    }
    uiLayout(state, 240.0f, 400.0f);
    virt = state.ent[list].try_get<UiVirtual>();
    CHECK(virt != nullptr);
    if (virt == nullptr) {
        return;
    }
    CHECK(virt->count == 40);
    CHECK(virt->slots > 12);
    CHECK(virt->slots <= kUiPoolCap);
    const uint16_t slot = virt->slot[0];
    const UiText* text = state.ent[slot].try_get<UiText>();
    CHECK(text != nullptr);
    if (text == nullptr) {
        return;
    }
    CHECK(std::strcmp(text->bytes, "0") == 0);
    CHECK(state.ent[virt->slot[virt->slots - 1]].try_get<UiVirtRow>()->index == static_cast<uint32_t>(virt->slots - 1));
}
TEST_CASE("backspace removes one utf8 character") {
    UiField field{};
    const unsigned char word[] = {0xD0, 0xB0, 0xD0, 0xB1, 0};
    std::memcpy(field.bytes, word, 4);
    field.len = 4;
    field.caret = 4;
    field.anchor = 4;
    fieldEraseBack(field);
    CHECK(field.len == 2);
    CHECK(static_cast<unsigned char>(field.bytes[0]) == 0xD0);
    CHECK(static_cast<unsigned char>(field.bytes[1]) == 0xB0);
}
TEST_CASE("msdf line places glyphs for mesh draw") {
    const char* word = "Burnhope";
    GlyphMetric atlas[8]{};
    for (uint32_t i = 0; i < 8; ++i) {
        atlas[i].codepoint = static_cast<uint8_t>(word[i]);
        atlas[i].advanceX = 10.0f;
        atlas[i].bearingX = 1.0f;
        atlas[i].bearingY = 8.0f;
        atlas[i].width = 8.0f;
        atlas[i].height = 10.0f;
        atlas[i].uvMin[0] = static_cast<float>(i) / 8.0f;
        atlas[i].uvMin[1] = 0.0f;
        atlas[i].uvMax[0] = static_cast<float>(i + 1) / 8.0f;
        atlas[i].uvMax[1] = 1.0f;
    }
    GlyphInstance glyphs[8]{};
    const uint32_t count = textLayoutLine(word, 8, atlas, 8, 0.0f, 20.0f, 200.0f, 100.0f, glyphs, 8);
    CHECK(count == 8);
    for (uint32_t i = 0; i < count; ++i) {
        const float clipX = (static_cast<float>(i) * 10.0f + 1.0f) * (2.0f / 200.0f) - 1.0f;
        CHECK(glyphs[i].posMin[0] == doctest::Approx(clipX));
        CHECK(glyphs[i].posMin[0] >= -1.0f);
        CHECK(glyphs[i].posMax[0] <= 1.0f);
        CHECK(glyphs[i].uvMax[0] > glyphs[i].uvMin[0]);
        CHECK(glyphs[i].uvMax[1] > glyphs[i].uvMin[1]);
        CHECK(glyphs[i].pxRange == doctest::Approx(kMsdfPxRange));
        if (i > 0) {
            CHECK(glyphs[i].posMin[0] > glyphs[i - 1].posMin[0]);
        }
    }
    GlyphDrawPush push{};
    push.glyphs = reinterpret_cast<uint64_t>(glyphs);
    push.count = count;
    MeshDrawDesc draw{};
    draw.groupCount = push.count;
    draw.pushData = &push;
    draw.pushBytes = sizeof(push);
    CHECK(draw.groupCount == 8);
    CHECK(draw.pushBytes == 16);
}
TEST_CASE("slider tracks the pointer") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(200, 22), paint(UiRole::Slider, 0.2f, 0.2f, 0.2f));
    state.ent[id].set<UiRange>({0.0f, 0.0f, 100.0f});
    uiLayout(state, 200.0f, 40.0f);
    InputFrame in{};
    in.pointer.x = state.box[id].x + state.box[id].w * 0.25f;
    in.pointer.y = state.box[id].y + 4.0f;
    in.pointer.pressed = kPointerLeft;
    in.pointer.down = kPointerLeft;
    uiApplyInput(state, in);
    const float mid = state.ent[id].try_get<UiRange>()->value;
    CHECK(mid == doctest::Approx(25.0f).epsilon(0.05));
    in.pointer.pressed = 0;
    in.pointer.x = state.box[id].x + state.box[id].w * 0.75f;
    uiApplyInput(state, in);
    CHECK(state.ent[id].try_get<UiRange>()->value == doctest::Approx(75.0f).epsilon(0.05));
}
