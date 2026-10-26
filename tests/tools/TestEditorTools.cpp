#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/widget/Widgets.hpp"
#include "ui/State.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Splitter.hpp"
#include "host/PropBind.hpp"
#include "host/PropImport.hpp"
#include "io/File.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneBlock.hpp"
#include "render/SceneFrame.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Virtual.hpp"
#include "debug/DebugStats.hpp"
#include "rhi/GpuProfiler.hpp"
#include "gpu_scene/FrameUniforms.hpp"
#include "gpu_scene/Camera.hpp"
#include "scene/TestScene.hpp"
#include "gpu_scene/SceneObject.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

using namespace burnhope;

TEST_CASE("double click maximizes the title only") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t btn = spawn(state, kUiNone, px(80, 24), paintRgb(0.2f, 0.2f, 0.2f));
    uiLayout(state, 200.0f, 80.0f);
    InputFrame in{};
    in.clicks = 2;
    in.pointer.x = state.box[btn].x + 2.0f;
    in.pointer.y = state.box[btn].y + 2.0f;
    (void)uiApplyInput(state, in);
    if (const WindowOps* plain = state.world.try_get<WindowOps>()) {
        CHECK(plain->toggleMaximize == false);
    }

    UiState titled;
    uiWidgetsInit(titled);
    const uint16_t root = spawn(titled, kUiNone, px(200.0f, 80.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t title = spawn(titled, root, px(180.0f, 32.0f), paintRgb(0.2f, 0.2f, 0.2f, 0.0f));
    titled.ent[title].set<UiTitleBar>({});
    uiLayout(titled, 200.0f, 80.0f);
    InputFrame click{};
    click.clicks = 2;
    click.pointer.pressed = kPointerLeft;
    click.pointer.down = kPointerLeft;
    click.pointer.x = titled.box[title].x + 4.0f;
    click.pointer.y = titled.box[title].y + 4.0f;
    (void)uiApplyInput(titled, click);
    const WindowOps* ops = titled.world.try_get<WindowOps>();
    CHECK(ops != nullptr);
    CHECK(ops->toggleMaximize);
}
TEST_CASE("splitter resizes the dock on the far side") {
    UiState state;
    uiWidgetsInit(state);
    UiFlex col{};
    col.widthMode = UiSize::Px;
    col.width = 200.0f;
    col.heightMode = UiSize::Px;
    col.height = 400.0f;
    const uint16_t root = spawn(state, kUiNone, col, paintRgb(0.1f, 0.1f, 0.1f));
    UiFlex grow{};
    grow.grow = 1.0f;
    grow.shrink = 1.0f;
    const uint16_t top = spawn(state, root, grow, paintRgb(0.2f, 0.2f, 0.2f));
    UiFlex bar = px(200.0f, 8.0f);
    UiPaint grip = paintRgb(0.3f, 0.3f, 0.3f);
    const uint16_t split = spawn(state, root, bar, grip);
    state.ent[split].set<UiSplitter>({});
    const uint16_t dock = spawn(state, root, px(200.0f, 120.0f), paintRgb(0.4f, 0.4f, 0.4f));
    uiLayout(state, 200.0f, 400.0f);
    CHECK(splitVertical(state, split));
    splitDrag(state, split, 40.0f, 250.0f);
    const UiFlex* flex = state.ent[dock].try_get<UiFlex>();
    CHECK(flex != nullptr);
    CHECK(flex->height == doctest::Approx(142.0f));
    CHECK(state.ent[top].try_get<UiFlex>()->grow == doctest::Approx(1.0f));
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.widthMode = UiSize::Px;
    row.width = 500.0f;
    row.heightMode = UiSize::Px;
    row.height = 80.0f;
    const uint16_t line = spawn(state, kUiNone, row, paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t left = spawn(state, line, px(140.0f, 80.0f), paintRgb(0.2f, 0.2f, 0.2f));
    UiFlex vbar = px(8.0f, 80.0f);
    UiPaint vgrip = paintRgb(0.3f, 0.3f, 0.3f);
    const uint16_t vsplit = spawn(state, line, vbar, vgrip);
    state.ent[vsplit].set<UiSplitter>({});
    spawn(state, line, grow, paintRgb(0.4f, 0.4f, 0.4f));
    state.box[line] = {0.0f, 0.0f, 500.0f, 80.0f};
    state.box[left] = {0.0f, 0.0f, 140.0f, 80.0f};
    state.box[vsplit] = {140.0f, 0.0f, 8.0f, 80.0f};
    CHECK(!splitVertical(state, vsplit));
    splitDrag(state, vsplit, 220.0f, 10.0f);
    CHECK(state.ent[left].try_get<UiFlex>()->width == doctest::Approx(220.0f));
}
TEST_CASE("inspector drag writes the bound float") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    block.scalars[0] = 0.5f;
    const uint16_t root = spawn(state, kUiNone, px(200.0f, 40.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t id = uiSlider(state, root, block.scalars[0], 0.0f, 1.0f, nullptr, nullptr, 200.0f);
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    binder.watch(id, 0, block.scalars[0]);
    uiLayout(state, 240.0f, 80.0f);
    const UiBox& box = state.box[id];
    dragSlider(state, id, box.x + box.w * 0.7f);
    binder.sync(state);
    CHECK(block.scalars[0] == doctest::Approx(0.7f).epsilon(0.02));
    GpuDebugStats stats{};
    stats.fps = 60.0f;
    stats.visibleInstances = 4;
    char hud[160]{};
    CHECK(debugFormatStats(stats, hud, sizeof(hud)) > 0);
    CHECK(std::strstr(hud, "60.0") != nullptr);
}
TEST_CASE("linked vec3 moves the other axes by the same delta, or by ratio") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    const uint16_t root = spawn(state, kUiNone, px(300.0f, 200.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t host = uiPropHost(state, root);
    const UiPropVec3 move = binder.vec3(state, host, "Location", 6, 1.0f, 2.0f, 3.0f, -100.0f, 100.0f, 0.1f);
    const UiPropVec3 scale = binder.vec3(state, host, "Scale", 12, 1.0f, 2.0f, 4.0f, -100.0f, 100.0f, 0.1f, true);
    auto* moveCtl = state.ent[move.lock].try_get_mut<UiVecCtl>();
    auto* scaleCtl = state.ent[scale.lock].try_get_mut<UiVecCtl>();
    CHECK(moveCtl != nullptr);
    CHECK(scaleCtl != nullptr);
    if (moveCtl == nullptr || scaleCtl == nullptr) {
        return;
    }
    uiRangeSet(state, move.x, 4.0f);
    binder.sync(state);
    CHECK(block.scalars[6] == doctest::Approx(4.0f));
    CHECK(block.scalars[7] == doctest::Approx(2.0f));
    moveCtl->linked = 1;
    scaleCtl->linked = 1;
    uiRangeSet(state, move.y, 5.0f);
    uiRangeSet(state, scale.x, 2.0f);
    binder.sync(state);
    CHECK(block.scalars[6] == doctest::Approx(7.0f));
    CHECK(block.scalars[7] == doctest::Approx(5.0f));
    CHECK(block.scalars[8] == doctest::Approx(6.0f));
    CHECK(block.scalars[12] == doctest::Approx(2.0f));
    CHECK(block.scalars[13] == doctest::Approx(4.0f));
    CHECK(block.scalars[14] == doctest::Approx(8.0f));
    binder.write(state, 6, 0.0f);
    binder.sync(state);
    CHECK(block.scalars[7] == doctest::Approx(5.0f));
}

TEST_CASE("slider and number share one scalar") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 40.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t number = binder.sliderNumber(state, root, 3, 0.25f, 0.0f, 1.0f, 0.01f);
    const uint16_t slider = binder.rows.back().pair;
    CHECK(slider != kUiNone);
    CHECK(block.scalars[3] == doctest::Approx(0.25f));
    uiLayout(state, 400.0f, 40.0f);
    const UiBox& box = state.box[slider];
    dragSlider(state, slider, box.x + box.w * 0.6f);
    binder.sync(state);
    float value = 0.0f;
    float minV = 0.0f;
    float maxV = 0.0f;
    CHECK(uiRange(state, number, value, minV, maxV));
    CHECK(block.scalars[3] == doctest::Approx(0.6f).epsilon(0.03));
    CHECK(value == doctest::Approx(block.scalars[3]));
    uiRangeSet(state, number, 0.9f);
    binder.sync(state);
    CHECK(block.scalars[3] == doctest::Approx(0.9f));
    CHECK(uiRange(state, slider, value, minV, maxV));
    CHECK(value == doctest::Approx(0.9f));
    binder.write(state, 3, 0.1f);
    CHECK(uiRange(state, slider, value, minV, maxV));
    CHECK(value == doctest::Approx(0.1f));
}

namespace {

struct ResetSeen {
    int calls = 0;
    uint8_t kind = 255;
    uint16_t index = 0;
};

void onResetSeen(void* user, UiState&, const UiResetHome& home) {
    auto* seen = static_cast<ResetSeen*>(user);
    seen->calls += 1;
    seen->kind = home.kind;
    seen->index = home.index;
}

} // namespace

TEST_CASE("reset buttons restore the scalar, the axes or the flag, then tell the app") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    ResetSeen seen;
    binder.onReset = onResetSeen;
    binder.resetUser = &seen;
    const uint16_t root = spawn(state, kUiNone, px(400.0f, 200.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t number = binder.sliderNumber(state, root, 3, 0.25f, 0.0f, 1.0f, 0.01f);
    const uint16_t floatBtn = binder.resetBtn(state, root, UiReset::Float, 3, 0.25f);
    const UiPropVec2 axes = binder.vec2Reset(state, root, "Tiling", 10, 1.0f, 2.0f);
    const uint16_t flagRow = binder.checkReset(state, root, "Double", 5, true);
    CHECK(floatBtn != kUiNone);
    CHECK(axes.row != kUiNone);
    CHECK(flagRow != kUiNone);

    binder.write(state, 3, 0.9f);
    activate(state, floatBtn);
    CHECK(block.scalars[3] == doctest::Approx(0.25f));
    CHECK(uiRangeVal(state, number, -1.0f) == doctest::Approx(0.25f));
    CHECK(seen.calls == 1);
    CHECK(seen.kind == static_cast<uint8_t>(UiReset::Float));

    binder.write(state, 10, 7.0f);
    binder.write(state, 11, 8.0f);
    uint16_t buttons[kUiCap];
    const uint16_t found = uiCollect<UiResetHome>(state, buttons, kUiCap);
    CHECK(found == 3);
    uint16_t vecBtn = kUiNone;
    uint16_t flagBtn = kUiNone;
    for (uint16_t i = 0; i < found; ++i) {
        const UiResetHome* home = state.ent[buttons[i]].try_get<UiResetHome>();
        if (home->index == 10) {
            vecBtn = buttons[i];
        } else if (home->index == 5) {
            flagBtn = buttons[i];
        }
    }
    activate(state, vecBtn);
    CHECK(block.scalars[10] == doctest::Approx(1.0f));
    CHECK(block.scalars[11] == doctest::Approx(2.0f));
    CHECK(seen.calls == 2);
    CHECK(seen.index == 10);

    block.flags[5] = 0;
    activate(state, flagBtn);
    CHECK(block.flags[5] == 1);
    CHECK(seen.kind == static_cast<uint8_t>(UiReset::Flag));
}

TEST_CASE("float window is a resizable panel, hidden unless asked") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(800.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t hidden = uiFloatWindow(state, root, 10.0f, 20.0f, 300.0f, 200.0f);
    const uint16_t shown = uiFloatWindow(state, root, 40.0f, 50.0f, 100.0f, 100.0f, true);
    CHECK(hidden != kUiNone);
    CHECK(state.ent[hidden].has<UiResizable>());
    CHECK(state.ent[shown].has<UiResizable>());
    CHECK_FALSE(nodeShown(state, hidden));
    CHECK(nodeShown(state, shown));
    CHECK(uiParent(state, hidden) == root);
}

TEST_CASE("range value falls back for a node that holds none") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(100.0f, 40.0f), paintRgb(0.1f, 0.1f, 0.1f));
    CHECK(uiRangeVal(state, root, 4.5f) == doctest::Approx(4.5f));
    CHECK(uiRangeVal(state, kUiNone, 2.0f) == doctest::Approx(2.0f));
}

TEST_CASE("colour hex field shows the scalars it owns") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    const uint16_t root = spawn(state, kUiNone, px(300.0f, 120.0f), paintRgb(0.1f, 0.1f, 0.1f));
    bool dirty = false;
    const uint16_t chip = binder.colorHex(state, root, 20, 30, true, &dirty);
    CHECK(chip < state.count);
    CHECK(binder.hexFields.size() == 1);
    CHECK(block.scalars[20] == doctest::Approx(1.0f));
    block.scalars[20] = 1.0f;
    block.scalars[21] = 0.5f;
    block.scalars[22] = 0.0f;
    block.scalars[30] = 1.0f;
    binder.syncSwatches(state);
    const UiField* field = state.ent[binder.hexFields[0]].try_get<UiField>();
    CHECK(field != nullptr);
    if (field == nullptr) {
        return;
    }
    CHECK(std::strcmp(field->bytes, "#FF8000") == 0);
    block.scalars[30] = 0.5f;
    binder.syncSwatches(state);
    CHECK(std::strlen(field->bytes) == 9);
    const UiPaint* paint = state.ent[chip].try_get<UiPaint>();
    CHECK(paint != nullptr);
    if (paint != nullptr) {
        CHECK(paint->g == doctest::Approx(0.5f));
    }
    CHECK(!dirty);
}

TEST_CASE("inspector json writes the slider index") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    const char* text =
        R"({"page":[{"role":"check","label":"Sun","bind":22,"on":true},{"role":"slider","label":"Gain","bind":13,"value":1.5,"min":0,"max":4}]})";
    const uint16_t root = spawn(state, kUiNone, px(220.0f, 80.0f), paintRgb(0.1f, 0.1f, 0.1f));
    CHECK(propImport(state, root, text, "page", binder, nullptr, 0, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr));
    CHECK(block.flags[22] == 1);
    CHECK(block.scalars[13] == doctest::Approx(1.5f));
    CHECK(binder.idOf(13, kBindFloat) != kUiNone);
    CHECK(binder.idOf(22, kBindFlag) != kUiNone);
    char path[512]{};
    MappedFile file{};
    CHECK(platformResolveAsset("assets/ui/inspector.json", path, sizeof(path)));
    CHECK(fileMapReadOnly(path, &file));
    const std::string_view doc{reinterpret_cast<const char*>(file.data), file.size};
    UiPropBinder page;
    page.attach(block.scalars, 256, block.flags, 64);
    CHECK(propImport(state, root, doc, "post", page, nullptr, 0, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr));
    CHECK(propImport(state, root, doc, "material", page, nullptr, 0, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr));
    CHECK(page.idOf(kScGiGain, kBindFloat) != kUiNone);
    CHECK(page.idOf(kFlRestirTemporal, kBindFlag) != kUiNone);
    CHECK(page.idOf(kScMatRough, kBindFloat) == kUiNone);
    CHECK(page.idOf(kScFogR, kBindSwatch) != kUiNone);
    CHECK(page.shown.size() == 9);
    fileUnmap(&file);
}
TEST_CASE("shell json names the spawn button") {
    UiState state;
    uiWidgetsInit(state);
    SceneBlock block{};
    UiPropBinder binder;
    binder.attach(block.scalars, 256, block.flags, 64);
    uint16_t spawnBtn = kUiNone;
    uint16_t actors = kUiNone;
    const PropSlot slots[] = {{"spawnBtn", &spawnBtn}, {"actors", &actors}};
    char path[512]{};
    MappedFile file{};
    CHECK(platformResolveAsset("assets/ui/editor.json", path, sizeof(path)));
    CHECK(fileMapReadOnly(path, &file));
    const std::string_view doc{reinterpret_cast<const char*>(file.data), file.size};
    const uint16_t root = spawn(state, kUiNone, px(800.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    CHECK(propImport(state, root, doc, "frame", binder, slots, 2, nullptr, 0, paintRgb(0.0f, 0.0f, 0.0f), nullptr, nullptr));
    CHECK(spawnBtn != kUiNone);
    CHECK(actors != kUiNone);
    CHECK(state.ent[actors].try_get<UiVirtual>() != nullptr);
    fileUnmap(&file);
}

TEST_CASE("virtual row keeps a long line") {
    UiState state;
    uiWidgetsInit(state);
    const uint16_t root = spawn(state, kUiNone, px(800.0f, 600.0f), paintRgb(0.1f, 0.1f, 0.1f));
    const uint16_t list = uiVirtual(state, root, 28.0f);
    char line[96];
    std::memset(line, 'A', 80);
    line[80] = '\0';
    auto fill = [](void* user, uint32_t, char* dst, uint8_t cap) {
        std::snprintf(dst, static_cast<size_t>(cap) + 1u, "%s", static_cast<const char*>(user));
    };
    uiVirtualSource(state, list, 1, fill, nullptr, line);
    bool kept = false;
    for (uint16_t id = 0; id < state.count; ++id) {
        const UiText* text = state.ent[id].try_get<UiText>();
        if (text != nullptr && text->len == 80) {
            kept = true;
        }
    }
    CHECK(kept);
}
TEST_CASE("pass mask follows the flag table") {
    SceneBlock block{};
    block.flags[kFlCluster] = 1;
    block.flags[kFlProbes] = 1;
    block.flags[kFlGi] = 3;
    block.flags[kFlAo] = 1;
    block.flags[kFlBloom] = 1;
    block.flags[kFlSun] = 1;
    block.flags[kFlPoints] = 1;
    block.flags[kFlSpots] = 1;
    block.flags[kFlArea] = 1;
    block.flags[kFlSky] = 1;
    block.flags[kFlCloud] = 1;
    block.flags[kFlReflect] = 2;
    block.flags[kFlDirect] = 1;
    block.flags[kFlRestirSun] = 1;
    block.flags[kFlAa] = 4;
    const uint32_t expect = 1u | 2u | 262144u | (1u << 23) | 16u | 32u | 256u | 512u | 65536u | 131072u | 1024u | 2048u | 8192u | (1u << 20) | 2097152u;
    CHECK(sceneBlockPassMask(block) == expect);
    block.flags[kFlAa] = 3;
    CHECK((sceneBlockPassMask(block) & (1u << 22)) != 0u);
    block.flags[kFlSunCsm] = 1;
    CHECK((sceneBlockPassMask(block) & 2097152u) == 0u);
    block.flags[kFlGi] = 1;
    CHECK((sceneBlockPassMask(block) & 262144u) == 0u);
    CHECK((sceneBlockPassMask(block) & 524288u) == 0u);
}
TEST_CASE("gpu profiler table names the slow pass") {
    CHECK(sizeof(FrameView) == 384);
    CHECK(sizeof(FrameSun) == 1920);
    CHECK(sizeof(FramePost) == 512);
    CHECK(sizeof(GtaoParams) == 64);
    CHECK(sizeof(SsrParams) == 64);
    GpuProfiler prof{};
    prof.timestamps = true;
    prof.count = 2;
    std::snprintf(prof.name[0], sizeof(prof.name[0]), "SSR_Trace");
    prof.ms[0] = 52.30f;
    std::snprintf(prof.name[1], sizeof(prof.name[1]), "Shade");
    prof.ms[1] = 3.10f;
    char table[256]{};
    CHECK(gpuProfilerFormat(prof, table, sizeof(table)) > 0);
    CHECK(std::strstr(table, "SSR_Trace") != nullptr);
    CHECK(std::strstr(table, "52.30") != nullptr);
    CHECK(std::strstr(table, "Shade") != nullptr);
}
TEST_CASE("scene actor duplicate keeps the transform") {
    SceneActors world;
    const float pos[3] = {1.0f, 2.0f, 3.0f};
    CHECK(sceneActorAdd(world, SceneActorKind::Sun, pos, "Sun") == 0);
    Xform pose = sceneActorXform(world, 0);
    pose.euler[1] = 30.0f;
    pose.scale[0] = 2.0f;
    sceneActorWriteXform(world, 0, pose);
    const uint8_t copy = sceneActorDuplicate(world);
    CHECK(copy == 1);
    CHECK(world.count == 2);
    CHECK(world.selected == 1);
    const Xform dup = sceneActorXform(world, 1);
    CHECK(dup.pos[0] == doctest::Approx(3.0f));
    CHECK(dup.pos[1] == doctest::Approx(2.0f));
    CHECK(dup.euler[1] == doctest::Approx(30.0f));
    CHECK(dup.scale[0] == doctest::Approx(2.0f));
    CHECK(actorKindOf(world, 1) == static_cast<uint8_t>(SceneActorKind::Sun));
    CHECK(world.ent[0].has<Xform>());
    CHECK(world.ent[0].has<SunObj>());
    CHECK(world.ent[0].has<PostObj>() == false);
    float x[3]{};
    float y[3]{};
    float z[3]{};
    actorBasis(pose.euler, x, y, z);
    CHECK(actorDot(x, y) == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(actorDot(y, z) == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(actorLen(z) == doctest::Approx(1.0f).epsilon(0.001));
    float spun[3]{};
    actorSpin(pose.euler, 0, 40.0f, spun);
    float x1[3]{};
    float y1[3]{};
    float z1[3]{};
    actorBasis(spun, x1, y1, z1);
    CHECK(actorDot(x, x1) == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(actorDot(y, y1) < 0.95f);
}
TEST_CASE("gizmo keeps a constant screen length") {
    CameraPose camPose{};
    camPose.eye[2] = 8.0f;
    camPose.aspect = 16.0f / 9.0f;
    const CameraCull nearCam = cameraCullBuild(camPose, nullptr);
    SceneActors world;
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    CHECK(sceneActorAdd(world, SceneActorKind::Sun, pos, "Sun") == 0);
    world.selected = 0;
    sceneActorSetCamera(world, nearCam, 1600.0f, 900.0f);
    const float nearPx = actorSpan(world, pos) / actorMetersPerPx(world, pos);
    camPose.eye[2] = 40.0f;
    const CameraCull farCam = cameraCullBuild(camPose, nullptr);
    sceneActorSetCamera(world, farCam, 1600.0f, 900.0f);
    const float farPx = actorSpan(world, pos) / actorMetersPerPx(world, pos);
    CHECK(nearPx == doctest::Approx(108.0f).epsilon(0.01));
    CHECK(farPx == doctest::Approx(108.0f).epsilon(0.01));
}

TEST_CASE("gizmo center follows the cursor in meters") {
    CameraPose camPose{};
    camPose.eye[1] = 2.0f;
    camPose.eye[2] = 6.0f;
    camPose.target[1] = 2.0f;
    camPose.aspect = 1600.0f / 900.0f;
    camPose.zn = 0.1f;
    camPose.zf = 80.0f;
    const CameraCull cam = cameraCullBuild(camPose, nullptr);
    SceneActors world;
    const float pos[3] = {0.0f, 2.0f, 0.0f};
    CHECK(sceneActorAdd(world, SceneActorKind::Sun, pos, "Sun") == 0);
    world.selected = 0;
    world.gizmo = 0;
    sceneActorSetCamera(world, cam, 1600.0f, 900.0f);
    float ox = 0.0f;
    float oy = 0.0f;
    CHECK(actorProject(world, pos, ox, oy));
    uint8_t part = kSceneActorNone;
    CHECK(sceneActorHit(world, ox, oy, part) == 0);
    CHECK(part == 6);
    sceneActorPointer(world, ox, oy, 1, 1, 0);
    CHECK(world.drag == 6);
    sceneActorPointer(world, ox + 30.0f, oy, 1, 0, 0);
    const Xform moved = sceneActorXform(world, 0);
    const float mpp = actorMetersPerPx(world, pos);
    CHECK(moved.pos[0] == doctest::Approx(30.0f * mpp).epsilon(0.2));
    CHECK(std::fabs(moved.pos[0]) < 2.0f);
    CHECK(moved.pos[1] == doctest::Approx(2.0f).epsilon(0.05));
}

TEST_CASE("gizmo axis follows the cursor") {
    CameraPose camPose{};
    camPose.eye[0] = 0.0f;
    camPose.eye[1] = 2.0f;
    camPose.eye[2] = 6.0f;
    camPose.target[0] = 0.0f;
    camPose.target[1] = 2.0f;
    camPose.target[2] = 0.0f;
    camPose.aspect = 1600.0f / 900.0f;
    camPose.zn = 0.1f;
    camPose.zf = 80.0f;
    const CameraCull cam = cameraCullBuild(camPose, nullptr);
    SceneActors world;
    const float pos[3] = {0.0f, 2.0f, 0.0f};
    CHECK(sceneActorAdd(world, SceneActorKind::Sun, pos, "Sun") == 0);
    sceneActorAdd(world, SceneActorKind::Post, pos, "Post");
    world.selected = 0;
    world.gizmo = 0;
    sceneActorSetCamera(world, cam, 1600.0f, 900.0f);
    const float span = actorSpan(world, pos);
    const float tip[3] = {span, 2.0f, 0.0f};
    const float further[3] = {span * 2.0f, 2.0f, 0.0f};
    float sx = 0.0f;
    float sy = 0.0f;
    float fx = 0.0f;
    float fy = 0.0f;
    CHECK(actorProject(world, tip, sx, sy));
    CHECK(actorProject(world, further, fx, fy));
    uint8_t axis = kSceneActorNone;
    CHECK(sceneActorHit(world, sx, sy, axis) == 0);
    CHECK(axis == 0);
    sceneActorPointer(world, sx, sy, 1, 1, 0);
    CHECK(world.drag == 0);
    sceneActorPointer(world, fx, fy, 1, 0, 0);
    const Xform moved = sceneActorXform(world, 0);
    CHECK(moved.pos[0] == doctest::Approx(span).epsilon(0.05));
    CHECK(moved.pos[1] == doctest::Approx(2.0f).epsilon(0.02));
    CHECK(moved.pos[2] == doctest::Approx(0.0f).epsilon(0.02));
    world.tool = 2;
    sceneActorFitTool(world);
    world.drag = kSceneActorNone;
    Xform back = sceneActorXform(world, 0);
    back.pos[0] = 0.0f;
    back.pos[1] = 2.0f;
    back.pos[2] = 0.0f;
    back.scale[0] = 1.0f;
    sceneActorWriteXform(world, 0, back);
    sceneActorPointer(world, sx, sy, 1, 1, 0);
    CHECK(world.drag == 0);
    sceneActorPointer(world, fx, fy, 1, 0, 0);
    CHECK(sceneActorXform(world, 0).scale[0] == doctest::Approx(2.0f).epsilon(0.08));
    world.tool = 1;
    sceneActorFitTool(world);
    world.drag = kSceneActorNone;
    const float yRing[3] = {span * 0.7071f, 2.0f, -span * 0.7071f};
    const float xRing[3] = {0.0f, 2.0f - span * 0.7071f, span * 0.7071f};
    float yx = 0.0f;
    float yy = 0.0f;
    float xx = 0.0f;
    float xy = 0.0f;
    CHECK(actorProject(world, yRing, yx, yy));
    CHECK(actorProject(world, xRing, xx, xy));
    axis = kSceneActorNone;
    CHECK(sceneActorHit(world, yx, yy, axis) == 0);
    CHECK(axis == 1);
    axis = kSceneActorNone;
    CHECK(sceneActorHit(world, xx, xy, axis) == 0);
    CHECK(axis == 0);
    sceneActorPointer(world, yx, yy, 1, 1, 0);
    CHECK(world.drag == 1);
    sceneActorPointer(world, yx, yy + 40.0f, 1, 0, 0);
    CHECK(std::fabs(sceneActorXform(world, 0).euler[1]) > 1.0f);
}
