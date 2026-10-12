#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/Domain.hpp"
#include "ui/State.hpp"
#include "ui/Layout.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/PropertyInspector.hpp"
#include "debug/DebugStats.hpp"
#include "rhi/GpuProfiler.hpp"
#include "gpu_scene/FrameUniforms.hpp"
#include "gpu_scene/Camera.hpp"
#include "scene/TestScene.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace burnhope;

TEST_CASE("double click maximizes the title only") {
    UiState state;
    const uint16_t btn = spawn(state, kUiNone, px(80, 24), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiLayout(state, 200.0f, 80.0f);
    InputFrame in{};
    in.clicks = 2;
    in.pointer.x = state.box[btn].x + 2.0f;
    in.pointer.y = state.box[btn].y + 2.0f;
    (void)uiApplyInput(state, in);
    CHECK(!state.opMaximize);

    UiState titled;
    const uint16_t root = spawn(titled, kUiNone, px(200.0f, 80.0f), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t title = spawn(titled, root, px(180.0f, 32.0f), paint(UiRole::Panel, 0.2f, 0.2f, 0.2f, 0.0f, -6));
    uiLayout(titled, 200.0f, 80.0f);
    InputFrame click{};
    click.clicks = 2;
    click.pointer.pressed = kPointerLeft;
    click.pointer.down = kPointerLeft;
    click.pointer.x = titled.box[title].x + 4.0f;
    click.pointer.y = titled.box[title].y + 4.0f;
    (void)uiApplyInput(titled, click);
    CHECK(titled.opMaximize);
}
TEST_CASE("splitter resizes the dock on the far side") {
    UiState state;
    UiFlex col{};
    col.widthMode = static_cast<uint8_t>(UiSize::Px);
    col.width = 200.0f;
    col.heightMode = static_cast<uint8_t>(UiSize::Px);
    col.height = 400.0f;
    const uint16_t root = spawn(state, kUiNone, col, paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex grow{};
    grow.grow = 1.0f;
    grow.shrink = 1.0f;
    const uint16_t top = spawn(state, root, grow, paint(UiRole::Panel, 0.2f, 0.2f, 0.2f));
    UiFlex bar = px(200.0f, 8.0f);
    UiPaint grip = paint(UiRole::Panel, 0.3f, 0.3f, 0.3f);
    grip.tag = -5;
    const uint16_t split = spawn(state, root, bar, grip);
    const uint16_t dock = spawn(state, root, px(200.0f, 120.0f), paint(UiRole::Panel, 0.4f, 0.4f, 0.4f));
    uiLayout(state, 200.0f, 400.0f);
    CHECK(splitVertical(state, split));
    splitDrag(state, split, 40.0f, 250.0f);
    const UiFlex* flex = state.ent[dock].try_get<UiFlex>();
    CHECK(flex != nullptr);
    CHECK(flex->height == doctest::Approx(142.0f));
    CHECK(state.ent[top].try_get<UiFlex>()->grow == doctest::Approx(1.0f));
    UiFlex row{};
    row.direction = 1;
    row.widthMode = static_cast<uint8_t>(UiSize::Px);
    row.width = 500.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = 80.0f;
    const uint16_t line = spawn(state, kUiNone, row, paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t left = spawn(state, line, px(140.0f, 80.0f), paint(UiRole::Panel, 0.2f, 0.2f, 0.2f));
    UiFlex vbar = px(8.0f, 80.0f);
    UiPaint vgrip = paint(UiRole::Panel, 0.3f, 0.3f, 0.3f);
    vgrip.tag = -5;
    const uint16_t vsplit = spawn(state, line, vbar, vgrip);
    spawn(state, line, grow, paint(UiRole::Panel, 0.4f, 0.4f, 0.4f));
    state.box[line] = {0.0f, 0.0f, 500.0f, 80.0f};
    state.box[left] = {0.0f, 0.0f, 140.0f, 80.0f};
    state.box[vsplit] = {140.0f, 0.0f, 8.0f, 80.0f};
    CHECK(!splitVertical(state, vsplit));
    splitDrag(state, vsplit, 220.0f, 10.0f);
    CHECK(state.ent[left].try_get<UiFlex>()->width == doctest::Approx(220.0f));
}
TEST_CASE("inspector drag writes the bound float") {
    float roughness = 0.5f;
    PropertyBinding prop{};
    prop.nameHash = assetHash("roughness");
    prop.type = PropType::FloatSlider;
    prop.minVal = 0.0f;
    prop.maxVal = 1.0f;
    prop.dataPtr = &roughness;
    InputState drag{};
    drag.mouseActive = 1;
    drag.deltaX = 40.0f;
    CHECK(inspectorHandleInput(&prop, 1, drag));
    CHECK(roughness == doctest::Approx(0.7f));
    char line[128]{};
    CHECK(inspectorWriteLines(&prop, 1, line, sizeof(line)) > 0);
    GpuDebugStats stats{};
    stats.fps = 60.0f;
    stats.visibleInstances = 4;
    char hud[160]{};
    CHECK(debugFormatStats(stats, hud, sizeof(hud)) > 0);
    CHECK(std::strstr(hud, "60.0") != nullptr);
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
    ActorView row[kSceneActorCap];
    actorLoad(world, row);
    CHECK(row[1].kind == static_cast<uint8_t>(SceneActorKind::Sun));
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
    world.gizmo = 2;
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
    world.gizmo = 1;
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
