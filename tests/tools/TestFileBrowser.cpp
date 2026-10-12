#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/Domain.hpp"
#include "ui/State.hpp"
#include "ui/Layout.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/widget/Detail.hpp"

#include <cstring>
#include <filesystem>

using namespace burnhope;

TEST_CASE("mounted file cells sit in the view") {
    namespace fs = std::filesystem;
    const fs::path dir = "/tmp/burnhope-grid-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Library", ec);
    UiState state;
    const uint16_t host = spawn(state, kUiNone, px(1600, 1050), paint(UiRole::Panel, 0.02f, 0.02f, 0.03f));
    UiFlex frame = px(1100, 700);
    frame.position = 1;
    frame.posX = 70.0f;
    frame.posY = 64.0f;
    frame.pad = 10.0f;
    frame.gap = 6.0f;
    uiFiles(state).fileFrame = spawn(state, host, frame, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f, -15));
    filesAttach(state);
    filesMount(state);
    filesShow(state, true);
    filesRoot(state, dir.c_str());
    uiLayout(state, 1600.0f, 1050.0f);
    InputFrame in{};
    uiApplyInput(state, in);
    uiLayout(state, 1600.0f, 1050.0f);
    const uint16_t view = uiFiles(state).fileView;
    uint16_t cell = kUiNone;
    for (uint16_t id = 0; id < state.count; ++id) {
        bool under = false;
        for (uint16_t p = state.parentOf[id]; p != kUiNone && p < state.count; p = state.parentOf[p]) {
            if (p == view) {
                under = true;
                break;
            }
        }
        if (!under) {
            continue;
        }
        const UiPaint* paint = state.ent[id].try_get<UiPaint>();
        if (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Button) && state.box[id].w > 40.0f) {
            cell = id;
            break;
        }
    }
    CHECK(view != kUiNone);
    CHECK(cell != kUiNone);
    if (cell == kUiNone) {
        fs::remove_all(dir, ec);
        return;
    }
    CHECK(state.box[cell].x == doctest::Approx(state.box[view].x + 8.0f).epsilon(0.05));
    CHECK(state.box[cell].y == doctest::Approx(state.box[view].y + 8.0f).epsilon(0.05));
    CHECK(state.box[cell].y > state.box[uiFiles(state).fileFrame].y + 40.0f);
    fs::remove_all(dir, ec);
}
TEST_CASE("file cell sits inside the view") {
    UiState state;
    const uint16_t host = spawn(state, kUiNone, px(800, 600), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    UiFlex frame = px(400, 300);
    frame.position = 1;
    frame.posX = 70.0f;
    frame.posY = 64.0f;
    const uint16_t win = spawn(state, host, frame, paint(UiRole::Panel, 0.1f, 0.1f, 0.12f));
    spawn(state, win, px(400, 40), paint(UiRole::Panel, 0.12f, 0.12f, 0.14f));
    UiFlex view{};
    view.grow = 1.0f;
    view.shrink = 1.0f;
    view.position = 2;
    const uint16_t grid = spawn(state, win, view, paint(UiRole::Panel, 0.05f, 0.05f, 0.07f));
    const uint16_t cell = spawn(state, grid, px(40, 40), paint(UiRole::Button, 0.9f, 0.8f, 0.2f));
    uiPlace(state, cell, true, 8.0f, 8.0f, 92.0f, 108.0f);
    uiLayout(state, 800.0f, 600.0f);
    CHECK(state.box[grid].y > state.box[win].y + 20.0f);
    CHECK(state.box[cell].x == doctest::Approx(state.box[grid].x + 8.0f));
    CHECK(state.box[cell].y == doctest::Approx(state.box[grid].y + 8.0f));
}
TEST_CASE("file new folder and zoom slider") {
    namespace fs = std::filesystem;
    const fs::path dir = "/tmp/burnhope-folder-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directory(dir, ec);
    UiState state;
    const uint16_t frame = spawn(state, kUiNone, px(900, 640), paint(UiRole::Panel, 0.08f, 0.08f, 0.1f));
    uiFiles(state).fileFrame = frame;
    filesAttach(state);
    filesMount(state);
    filesShow(state, true);
    filesRoot(state, dir.c_str());
    uiLayout(state, 900.0f, 640.0f);
    uint16_t neu = kUiNone;
    uint16_t zoom = kUiNone;
    for (uint16_t id = 0; id < state.count; ++id) {
        const UiPaint* paint = state.ent[id].try_get<UiPaint>();
        const UiText* text = state.ent[id].try_get<UiText>();
        if (paint == nullptr) {
            continue;
        }
        if (paint->role == static_cast<uint8_t>(UiRole::Button) && text != nullptr && std::strcmp(text->bytes, "+") == 0) {
            neu = id;
        }
        if (paint->role == static_cast<uint8_t>(UiRole::Slider)) {
            zoom = id;
        }
    }
    CHECK(neu != kUiNone);
    CHECK(zoom != kUiNone);
    if (neu != kUiNone && state.box[neu].w > 1.0f) {
        InputFrame in{};
        in.pointer.x = state.box[neu].x + state.box[neu].w * 0.5f;
        in.pointer.y = state.box[neu].y + state.box[neu].h * 0.5f;
        in.pointer.pressed = kPointerLeft;
        in.pointer.down = kPointerLeft;
        uiApplyInput(state, in);
        uiLayout(state, 900.0f, 640.0f);
        uint16_t folder = kUiNone;
        for (uint16_t id = 0; id < state.count; ++id) {
            const UiText* text = state.ent[id].try_get<UiText>();
            const UiPaint* paint = state.ent[id].try_get<UiPaint>();
            if (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Button) && text != nullptr
                && std::strcmp(text->bytes, "New Folder") == 0 && state.box[id].w > 1.0f) {
                folder = id;
            }
        }
        if (folder != kUiNone) {
            in.pointer.pressed = kPointerLeft;
            in.pointer.down = kPointerLeft;
            in.pointer.x = state.box[folder].x + 8.0f;
            in.pointer.y = state.box[folder].y + 8.0f;
            uiApplyInput(state, in);
        }
    }
    bool created = false;
    if (fs::is_directory(dir, ec)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().filename().string().find("New Folder") != std::string::npos) {
                created = true;
            }
        }
    }
    CHECK(created);
    if (zoom != kUiNone && state.box[zoom].w > 1.0f) {
        InputFrame in{};
        in.pointer.x = state.box[zoom].x + state.box[zoom].w * 0.8f;
        in.pointer.y = state.box[zoom].y + state.box[zoom].h * 0.5f;
        in.pointer.pressed = kPointerLeft;
        in.pointer.down = kPointerLeft;
        uiApplyInput(state, in);
        const UiRange* range = state.ent[zoom].try_get<UiRange>();
        CHECK(range != nullptr);
        if (range != nullptr) {
            CHECK(range->value > 100.0f);
        }
    }
    filesShutdown(state);
    fs::remove_all(dir, ec);
}
