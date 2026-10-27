#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "asset/ModelThumb.hpp"
#include "gui/Gui.hpp"
#include "gui/data/GuiRow.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/loader/GuiLoader.hpp"
#include "gui/overlay/GuiTooltip.hpp"
#include "gui/style/GuiStyleSheet.hpp"

using namespace burnhope;
using namespace burnhope::gui;

namespace {

constexpr const char* kStyles = R"({
  "classes": {
    "root":    { "direction": "column" },
    "button":  { "w": 120, "h": 30, "bg": "button", "alpha": 1, "textSize": 14 },
    "tooltip": { "position": "absolute", "w": 200, "h": 40, "padH": 8, "padV": 4, "bg": "menu", "alpha": 1, "textSize": 12 }
  }
})";

constexpr const char* kScreen = R"({
  "name": "shell", "class": "root",
  "children": [
    { "name": "save", "bind": "file.save", "class": "button", "text": "Save", "tooltip": "Writes the project", "behaviors": ["hover"] },
    { "name": "plain", "class": "button", "text": "Plain", "behaviors": ["hover"] },
    { "name": "tip", "class": "tooltip", "behaviors": ["overlay"], "placement": "pointer", "dismiss": false, "tooltipHost": true,
      "passive": true, "text": "" }
  ]
})";

struct Shell {
    flecs::world world;
    GuiStyleSheet sheet{};
    GuiContext ctx{};
    flecs::entity root;

    bool open() {
        ctx.style = &sheet;
        return guiStyleLoad(sheet, kStyles) && guiInit(ctx, world) && guiLoadScreen(world, sheet, kScreen, flecs::entity(), &root);
    }

    void frame(float x, float y) {
        InputFrame in{};
        in.pointer.x = x;
        in.pointer.y = y;
        guiConsumeInput(ctx, in);
        guiLayout(ctx, 400.0f, 300.0f);
    }
};

} // namespace

TEST_CASE("tooltip: a node's JSON tip opens at the pointer after the delay and closes when the pointer leaves") {
    Shell s;
    CHECK(s.open());
    if (!(s.open())) {
        return;
    }
    s.frame(0.0f, 0.0f);
    const flecs::entity save = s.root.lookup("shell::save").is_valid() ? s.root.lookup("shell::save") : s.root.lookup("save");
    const flecs::entity tip = s.root.lookup("shell::tip").is_valid() ? s.root.lookup("shell::tip") : s.root.lookup("tip");
    CHECK(save.is_valid());
    if (!(save.is_valid())) {
        return;
    }
    CHECK(tip.is_valid());
    if (!(tip.is_valid())) {
        return;
    }
    CHECK(save.has<UiTooltipText>());
    CHECK(std::strcmp(save.try_get<UiBindKey>()->key, "file.save") == 0);
    const UiBox box = *save.try_get<UiBox>();
    const float px = box.x + 10.0f;
    const float py = box.y + 10.0f;
    s.frame(px, py);
    CHECK(tip.try_get<UiOverlay>()->open == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(kGuiTooltipDelay * 1000.0f) + 80));
    s.frame(px, py);
    CHECK(tip.try_get<UiOverlay>()->open == 1);
    CHECK(std::strcmp(tip.try_get<UiText>()->text, "Writes the project") == 0);
    const UiBox shown = *tip.try_get<UiBox>();
    CHECK(shown.x >= 0.0f);
    CHECK(shown.x + shown.w <= 400.0f);
    CHECK(shown.y + shown.h <= 300.0f);
    s.frame(399.0f, 299.0f);
    CHECK(tip.try_get<UiOverlay>()->open == 0);
    guiLayoutDetach(s.root);
}

TEST_CASE("tooltip: a node without tip text never opens it") {
    Shell s;
    CHECK(s.open());
    if (!(s.open())) {
        return;
    }
    s.frame(0.0f, 0.0f);
    const flecs::entity plain = s.root.lookup("shell::plain").is_valid() ? s.root.lookup("shell::plain") : s.root.lookup("plain");
    const flecs::entity tip = s.root.lookup("shell::tip").is_valid() ? s.root.lookup("shell::tip") : s.root.lookup("tip");
    CHECK(plain.is_valid());
    if (!(plain.is_valid())) {
        return;
    }
    const UiBox box = *plain.try_get<UiBox>();
    s.frame(box.x + 5.0f, box.y + 5.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(kGuiTooltipDelay * 1000.0f) + 80));
    s.frame(box.x + 5.0f, box.y + 5.0f);
    CHECK(tip.try_get<UiOverlay>()->open == 0);
    guiLayoutDetach(s.root);
}

TEST_CASE("row fields: only a zeroed row is plain") {
    GuiRow row{};
    CHECK(!guiRowActive(row));
    row.depth = 2;
    CHECK(guiRowActive(row));
    row = {};
    std::strcpy(row.col2, "1 KB");
    CHECK(guiRowActive(row));
}

TEST_CASE("model thumb: a cube fills the middle of the cell, the corners keep the plate") {
    ModelMesh mesh;
    const float c[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    for (const auto& v : c) {
        mesh.xyz.insert(mesh.xyz.end(), v, v + 3);
    }
    const uint32_t tris[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    for (const auto& t : tris) {
        mesh.idx.insert(mesh.idx.end(), t, t + 3);
    }
    const uint8_t plate[3] = {38, 40, 48};
    uint8_t rgba[kModelThumbBytes];
    CHECK(modelBakeThumb(mesh, plate, rgba));
    if (!(modelBakeThumb(mesh, plate, rgba))) {
        return;
    }
    const uint8_t* centre = rgba + ((kModelThumbPx / 2) * kModelThumbPx + kModelThumbPx / 2) * 4;
    CHECK((centre[0] != plate[0] || centre[1] != plate[1] || centre[2] != plate[2]));
    CHECK(rgba[0] == plate[0]);
    CHECK(rgba[1] == plate[1]);
    CHECK(rgba[2] == plate[2]);
    CHECK(rgba[3] == 255);
    ModelMesh empty;
    CHECK(!modelBakeThumb(empty, plate, rgba));
}
