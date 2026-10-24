#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/widget/Widgets.hpp"
#include "ui/State.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <cstring>

using namespace burnhope;

TEST_CASE("blob import keeps the name") {
    BinHead head{};
    head.count = 1;
    head.pool = 5;
    BinNode node{};
    node.role = kBinSlotButton;
    node.widthMode = static_cast<uint8_t>(UiSize::Px);
    node.heightMode = static_cast<uint8_t>(UiSize::Px);
    node.width = 80.0f;
    node.height = 24.0f;
    node.a = 1.0f;
    node.shown = 1;
    node.nameOff = 0;
    node.nameLen = 4;
    node.textOff = 0;
    node.textLen = 4;
    BinTail role{};
    unsigned char blob[sizeof(BinHead) + sizeof(BinNode) + sizeof(BinTail) + 8]{};
    std::memcpy(blob, &head, sizeof(head));
    std::memcpy(blob + sizeof(head), &node, sizeof(node));
    std::memcpy(blob + sizeof(head) + sizeof(node), &role, sizeof(role));
    std::memcpy(blob + sizeof(head) + sizeof(node) + sizeof(role), "save", 4);
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = uiImportBin(state, kUiNone, blob, sizeof(head) + sizeof(node) + sizeof(role) + 5);
    CHECK(root != kUiNone);
    CHECK(uiFindName(state, "save") == root);
    const UiPaint* paint = state.ent[root].try_get<UiPaint>();
    CHECK(paint != nullptr);
    CHECK(paint->token == UiToken::Button);
    const UiText* title = state.ent[root].try_get<UiText>();
    CHECK(title != nullptr);
    CHECK(std::strcmp(title->bytes, "save") == 0);
}
TEST_CASE("broken blobs do not import") {
    unsigned char junk[64];
    for (int i = 0; i < 64; ++i) {
        junk[i] = static_cast<unsigned char>(i * 17 + 3);
    }
    UiState state;
    uiWidgetsInit(state);
    CHECK(uiImportBin(state, kUiNone, junk, 64) == kUiNone);
    CHECK(uiImportBin(state, kUiNone, nullptr, 0) == kUiNone);
    BinHead head{};
    head.magic = 0;
    CHECK(uiImportBin(state, kUiNone, &head, sizeof(head)) == kUiNone);
    head.magic = kUiBinMagic;
    head.version = 2;
    head.count = 4;
    head.pool = 0;
    CHECK(uiImportBin(state, kUiNone, &head, sizeof(head)) == kUiNone);
    head.count = 1;
    head.pool = 4000;
    BinNode node{};
    node.shown = 1;
    node.a = 1.0f;
    unsigned char cut[sizeof(BinHead) + sizeof(BinNode)];
    std::memcpy(cut, &head, sizeof(head));
    std::memcpy(cut + sizeof(head), &node, sizeof(node));
    CHECK(uiImportBin(state, kUiNone, cut, sizeof(cut)) == kUiNone);
    BinNode named{};
    named.shown = 1;
    named.parent = 0xffff;
    named.width = 40.0f;
    named.height = 20.0f;
    named.a = 1.0f;
    named.nameOff = 100;
    named.nameLen = 4;
    head.count = 1;
    head.pool = 4;
    unsigned char namedBuf[sizeof(BinHead) + sizeof(BinNode) + 4];
    std::memcpy(namedBuf, &head, sizeof(head));
    std::memcpy(namedBuf + sizeof(head), &named, sizeof(named));
    CHECK(uiImportBin(state, kUiNone, namedBuf, sizeof(namedBuf)) == kUiNone);
    CHECK(state.count == 0);
}
TEST_CASE("baked scale slider moves") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = uiImportBinFile(state, kUiNone, "build-core/ui/shell.uib");
    CHECK(root != kUiNone);
    if (root == kUiNone) {
        return;
    }
    const uint16_t scale = uiFindName(state, "scale");
    CHECK(scale != kUiNone);
    if (scale == kUiNone) {
        return;
    }
    if (const uint16_t scene = uiFindName(state, "page_scene"); scene != kUiNone) {
        uiShow(state, scene, false);
    }
    if (const uint16_t input = uiFindName(state, "page_input"); input != kUiNone) {
        uiShow(state, input, false);
    }
    if (const uint16_t look = uiFindName(state, "page_look"); look != kUiNone) {
        uiShow(state, look, true);
    }
    uiLayout(state, 1600.0f, 900.0f);
    CHECK(state.box[scale].w > 1.0f);
    const UiRange* before = state.ent[scale].try_get<UiRange>();
    CHECK(before != nullptr);
    if (before == nullptr || state.box[scale].w <= 1.0f) {
        return;
    }
    CHECK(before->max > before->min);
    InputFrame in{};
    in.pointer.x = state.box[scale].x + state.box[scale].w * 0.8f;
    in.pointer.y = state.box[scale].y + state.box[scale].h * 0.5f;
    in.pointer.pressed = kPointerLeft;
    in.pointer.down = kPointerLeft;
    (void)uiApplyInput(state, in);
    const UiRange* after = state.ent[scale].try_get<UiRange>();
    CHECK(after->value == doctest::Approx(before->min + 0.8f * (before->max - before->min)).epsilon(0.08));
}
