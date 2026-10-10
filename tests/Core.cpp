#define DOCTEST_CONFIG_NO_EXCEPTIONS
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ui/State.hpp"
#include "ui/Layout.hpp"
#include "ui/widget/Detail.hpp"
#include "files/Browser.hpp"
#include "memory/FrameArena.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"
#include "gpu_scene/FrameUniforms.hpp"
#include "rhi/GpuBuffer.hpp"
#include "image/ImageFile.hpp"
#include "ui/Canvas.hpp"
#include "gpu_scene/Camera.hpp"
#include "gpu_scene/InstanceCull.hpp"
#include "gpu_scene/MeshletCull.hpp"
#include "gpu_scene/SunShadow.hpp"
#include "gpu_scene/LightMath.hpp"
#include "render/PassDebug.hpp"
#include "render/VisResolve.hpp"
#include "gfx/PrimitiveGen.hpp"
#include "gfx/MeshletBuilder.hpp"
#include "gfx/Portal.hpp"
#include "scene/FlyCam.hpp"
#include "scene/TestScene.hpp"
#include "stream/StreamRing.hpp"
#include "stream/StreamQueue.hpp"
#include "stream/ResidencyTable.hpp"
#include "gpu_scene/PointShadow.hpp"
#include "wasm/WasmHost.hpp"
#include "anim/Animation.hpp"
#include "time/Time.hpp"
#include "ui/Style.hpp"
#include "input/Input.hpp"
#include "io/File.hpp"
#include "io/Blob.hpp"
#include "gfx/Material.hpp"
#include "gfx/Meshlet.hpp"
#include "gfx/Light.hpp"
#include "gfx/ClusterGrid.hpp"
#include "gfx/HiZ.hpp"
#include "ui/PropertyInspector.hpp"
#include "debug/DebugStats.hpp"
#include "color/Color.hpp"
#include "doc/Document.hpp"
#include "text/TextLayout.hpp"
#include "rhi/MeshDraw.hpp"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <vector>
#include <spdlog/spdlog.h>

namespace burnhope {
void uiBuildShell(UiState&) {}
}

using namespace burnhope;

TEST_CASE("layout dump names the node") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(80, 24), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiName(state, id, "save");
    uiLayout(state, 200.0f, 80.0f);
    uiTrace(state, "test", id);
    uiDumpLayout(state);
    CHECK(state.box[id].w > 1.0f);
    CHECK(std::strcmp(state.ent[id].try_get<UiName>()->bytes, "save") == 0);
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
    state.fileFrame = spawn(state, host, frame, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f, -15));
    filesAttach(state);
    filesMount(state);
    filesShow(state, true);
    filesRoot(state, dir.c_str());
    uiLayout(state, 1600.0f, 1050.0f);
    InputFrame in{};
    uiApplyInput(state, in);
    uiLayout(state, 1600.0f, 1050.0f);
    const uint16_t view = state.fileView;
    uint16_t cell = kUiNone;
    for (uint16_t id = 0; id < state.count; ++id) {
        if (state.parentOf[id] == view) {
            const UiPaint* paint = state.ent[id].try_get<UiPaint>();
            if (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Button) && state.box[id].w > 1.0f) {
                cell = id;
                break;
            }
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
    CHECK(state.box[cell].y > state.box[state.fileFrame].y + 40.0f);
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

TEST_CASE("curve is a gpu ribbon") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(180, 80), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    state.ent[id].set<UiCurve>({});
    uiLayout(state, 200.0f, 100.0f);
    UiPrimitive prims[128];
    const uint32_t n = uiEmit(state, prims, 128);
    int ribbons = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if ((prims[i].flags & kUiFlagRibbon) != 0) {
            ++ribbons;
        }
    }
    CHECK(ribbons == 16);
}

TEST_CASE("yoga box") {
    UiState state;
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = 120.0f;
    flex.height = 40.0f;
    const uint16_t root = spawn(state, kUiNone, flex, paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex childFlex{};
    childFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    childFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    childFlex.width = 120.0f;
    childFlex.height = 40.0f;
    const uint16_t child = spawn(state, root, childFlex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiLayout(state, 400.0f, 300.0f);
    CHECK(state.box[root].w == doctest::Approx(400.0f));
    CHECK(state.box[child].w == doctest::Approx(120.0f));
    CHECK(state.box[child].h == doctest::Approx(40.0f));
    CHECK(state.ent[child].try_get<UiBounds>() != nullptr);
}

TEST_CASE("tab index wraps") {
    UiState state;
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = 80.0f;
    flex.height = 24.0f;
    const uint16_t a = spawn(state, kUiNone, flex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    const uint16_t b = spawn(state, kUiNone, flex, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiTab(state, a, 2);
    uiTab(state, b, 1);
    uiLayout(state, 200.0f, 200.0f);
    uiFocus(state, a);
    uiFocusStep(state, 1);
    CHECK(state.focused == b);
    uiFocusStep(state, 1);
    CHECK(state.focused == a);
}

TEST_CASE("scroll scissor blocks the child") {
    UiState state;
    UiFlex host{};
    host.widthMode = static_cast<uint8_t>(UiSize::Px);
    host.heightMode = static_cast<uint8_t>(UiSize::Px);
    host.width = 200.0f;
    host.height = 200.0f;
    const uint16_t root = spawn(state, kUiNone, host, paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    UiFlex scrollFlex{};
    scrollFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
    scrollFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    scrollFlex.width = 100.0f;
    scrollFlex.height = 40.0f;
    scrollFlex.overflow = 2;
    const uint16_t scroll = spawn(state, root, scrollFlex, paint(UiRole::Scroll, 0.1f, 0.1f, 0.1f));
    UiFlex child{};
    child.position = 1;
    child.posY = 80.0f;
    child.widthMode = static_cast<uint8_t>(UiSize::Px);
    child.heightMode = static_cast<uint8_t>(UiSize::Px);
    child.width = 40.0f;
    child.height = 20.0f;
    const uint16_t button = spawn(state, scroll, child, paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    uiLayout(state, 200.0f, 200.0f);
    const uint16_t hit = hitTest(state, state.box[button].x + 2.0f, state.box[button].y + 2.0f);
    CHECK(hit != button);
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

TEST_CASE("blob import keeps the name") {
    BinHead head{};
    head.count = 1;
    head.pool = 5;
    BinNode node{};
    node.parent = 0xffff;
    node.role = static_cast<uint8_t>(UiRole::Button);
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
    unsigned char blob[sizeof(BinHead) + sizeof(BinNode) + 8]{};
    std::memcpy(blob, &head, sizeof(head));
    std::memcpy(blob + sizeof(head), &node, sizeof(node));
    std::memcpy(blob + sizeof(head) + sizeof(node), "save", 4);
    UiState state;
    const uint16_t root = uiImportBin(state, kUiNone, blob, sizeof(head) + sizeof(node) + 5);
    CHECK(root != kUiNone);
    CHECK(uiFindName(state, "save") == root);
    const UiPaint* paint = state.ent[root].try_get<UiPaint>();
    CHECK(paint != nullptr);
    CHECK(paint->role == static_cast<uint8_t>(UiRole::Button));
    const UiText* title = state.ent[root].try_get<UiText>();
    CHECK(title != nullptr);
    CHECK(std::strcmp(title->bytes, "save") == 0);
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
    CHECK(virt->count == 40);
    CHECK(virt->slots == 12);
    const uint16_t slot = virt->slot[0];
    const UiText* text = state.ent[slot].try_get<UiText>();
    CHECK(text != nullptr);
    if (text == nullptr) {
        return;
    }
    CHECK(std::strcmp(text->bytes, "0") == 0);
    CHECK(state.ent[virt->slot[11]].try_get<UiVirtRow>()->index == 11);
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

TEST_CASE("arena reset drops the bump") {
    FrameArena arena;
    CHECK(frameArenaCreate(arena, 4096));
    CHECK(frameArenaAlloc(arena, 128) != nullptr);
    CHECK(arena.off > 0);
    frameArenaReset(arena);
    CHECK(arena.off == 0);
    frameArenaDestroy(arena);
    CHECK(arena.mem == nullptr);
}

TEST_CASE("primitive keeps clip and four radii") {
    CHECK(sizeof(UiPrimitive) == 80);
    CHECK(offsetof(UiPrimitive, extra) == 48);
    CHECK(offsetof(UiPrimitive, corner) == 64);
    UiPrimitive prim{};
    prim.corner[0] = 2;
    prim.corner[1] = 4;
    prim.corner[2] = 6;
    prim.corner[3] = 8;
    prim.extra[0] = 2;
    CHECK(prim.corner[2] == doctest::Approx(6.0f));
}

// Фаза 5 (план рендера): rhi/MeshDraw.hpp — единая точка одного draw, которой уже пользуются
// и core/ui/Canvas.cpp (mesh + vertex-pull fallback), и engine/render/Visbuffer.cpp,
// engine/render/Tonemap.cpp (mesh only). Здесь проверяется just сама логика выбора пути без
// реального GPU: ни mesh, ни vertex-pull не настроены — функция обязана не трогать cmd вообще
// (VK_NULL_HANDLE cmd must stay safe to pass). Путь с реальным устройством уже покрыт
// "golden image: panel fill is exact and mesh/vertex paths match" — та же функция, два пути.
TEST_CASE("meshDrawRecord is a no-op with no shaders bound") {
    const MeshDrawDesc none{};
    meshDrawRecord(VK_NULL_HANDLE, none);
    const MeshDrawDesc zeroCounts{.mesh = reinterpret_cast<VkShaderEXT>(1), .vert = reinterpret_cast<VkShaderEXT>(2), .frag = reinterpret_cast<VkShaderEXT>(3)};
    meshDrawRecord(VK_NULL_HANDLE, zeroCounts);
    CHECK(true); // reaching here without touching cmd means both no-shader and zero-count paths stayed no-op
}

TEST_CASE("transform/filter pack round trip through pad0/pad1") {
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uiPackTransform(pad0, pad1, 45.0f, 1.5f, 0.5f, 1.2f, 0.8f);
    float angle = 0, sx = 0, sy = 0, br = 0, ct = 0;
    uiUnpackTransform(pad0, pad1, angle, sx, sy, br, ct);
    CHECK(angle == doctest::Approx(45.0f).epsilon(0.01));
    CHECK(sx == doctest::Approx(1.5f).epsilon(0.001));
    CHECK(sy == doctest::Approx(0.5f).epsilon(0.001));
    CHECK(br == doctest::Approx(1.2f).epsilon(0.01));
    CHECK(ct == doctest::Approx(0.8f).epsilon(0.01));
}

TEST_CASE("transform pack handles range edges") {
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uiPackTransform(pad0, pad1, -180.0f, 0.0f, 4.0f, 0.0f, 2.0f);
    float angle = 0, sx = 0, sy = 0, br = 0, ct = 0;
    uiUnpackTransform(pad0, pad1, angle, sx, sy, br, ct);
    CHECK(angle == doctest::Approx(-180.0f).epsilon(0.01));
    CHECK(sx == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(sy == doctest::Approx(4.0f).epsilon(0.01));
    CHECK(br == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(ct == doctest::Approx(2.0f).epsilon(0.01));
}

TEST_CASE("device lost flag stops draws until cleared") {
    Device device{};
    CHECK(!deviceLost(device));
    deviceMarkLost(device, "test", static_cast<int32_t>(VK_ERROR_DEVICE_LOST));
    CHECK(deviceLost(device));
    deviceMarkLost(device, "test", static_cast<int32_t>(VK_ERROR_DEVICE_LOST));
    CHECK(deviceLost(device));
    device.lost.store(0);
    CHECK(!deviceLost(device));
}

TEST_CASE("camera cull feeds frustum cascades and froxels") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 0.0f;
    pose.target[2] = 0.0f;
    pose.aspect = 1.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    CHECK(cameraSphereVisible(cam, 0.0f, 0.0f, 0.0f, 0.2f));
    CHECK(!cameraSphereVisible(cam, 0.0f, 0.0f, 20.0f, 0.2f));
    CHECK(cam.cascadeCount == 4);
    CHECK(cam.cascadeSplit[0] == doctest::Approx(cam.zn));
    CHECK(cam.cascadeSplit[4] == doctest::Approx(cam.zf));
    CHECK(cam.cascadeSplit[1] > cam.cascadeSplit[0]);
    CHECK(cam.froxelCount == 16);
    CHECK(cameraFroxelSlice(cam, cam.zn) == 0);
    CHECK(cameraFroxelSlice(cam, cam.zf) == 15);
    CHECK(cameraConeVisible(cam, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    CHECK(!cameraConeVisible(cam, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.9f));
    CHECK(cam.viewProj.m[11] == doctest::Approx(-1.0f));
}

TEST_CASE("cullInstances keeps spheres inside the frustum") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 0.0f;
    pose.target[2] = 0.0f;
    pose.aspect = 1.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    GpuInstance scene[12]{};
    for (int i = 0; i < 8; ++i) {
        instanceSet(scene[i], static_cast<float>(i) * 0.15f, 0.0f, 0.0f, 0.2f, static_cast<uint32_t>(i), 0);
    }
    for (int i = 0; i < 4; ++i) {
        instanceSet(scene[8 + i], 0.0f, 0.0f, 30.0f, 0.2f, 100, 1);
    }
    uint32_t visible[12]{};
    const uint32_t n = cullInstances(cam, scene, 12, visible, 12);
    CHECK(n == 8);
    for (uint32_t i = 0; i < n; ++i) {
        CHECK(visible[i] == i);
        CHECK(scene[visible[i]].sphere[2] == doctest::Approx(0.0f));
    }
    CHECK(!cameraSphereVisible(cam, scene[8].sphere[0], scene[8].sphere[1], scene[8].sphere[2], scene[8].sphere[3]));
    CHECK(sizeof(GpuInstance) == 80);
    CHECK(n == 8);
}

TEST_CASE("anim track samples linear loop and cubic") {
    const AnimKey keys[] = {{0.0f, 0.0f}, {1.0f, 100.0f}};
    AnimTrack linear{};
    linear.keys = keys;
    linear.count = 2;
    linear.ease = AnimEase::Linear;
    linear.wrap = AnimWrap::Loop;
    linear.duration = 1.0f;
    CHECK(animSampleTrack(linear, 0.5f) == doctest::Approx(50.0f));
    CHECK(animSampleTrack(linear, 1.5f) == doctest::Approx(50.0f));
    AnimPlayer player{};
    player.playing = true;
    player.speed = 1.0f;
    player.duration = 1.0f;
    player.wrap = AnimWrap::Loop;
    animTickPlayers(&player, 1, 1.5f);
    CHECK(player.currentTime == doctest::Approx(0.5f));
    CHECK(player.playing);
    AnimTrack cubic = linear;
    cubic.ease = AnimEase::CubicBezier;
    cubic.wrap = AnimWrap::Once;
    CHECK(animSampleTrack(cubic, 0.5f) == doctest::Approx(50.0f).epsilon(0.02));
    CHECK(animSampleTrack(cubic, 0.25f) < 25.0f);
    CHECK(animSampleTrack(cubic, 0.0f) == doctest::Approx(0.0f));
    CHECK(animSampleTrack(cubic, 1.0f) == doctest::Approx(100.0f));
    AnimTrack step = linear;
    step.ease = AnimEase::Step;
    step.wrap = AnimWrap::Once;
    CHECK(animSampleTrack(step, 0.9f) == doctest::Approx(0.0f));
}

TEST_CASE("fixed step consumes remainder and clamps a spike") {
    EngineTime time{};
    time.fixedDelta = 0.02f;
    time.maxAccumulation = 0.1f;
    int ticks = 0;
    timeAdvance(time, 0.05f, [&](float step) {
        CHECK(step == doctest::Approx(0.02f));
        ticks += 1;
    });
    CHECK(ticks == 2);
    CHECK(time.accumulator == doctest::Approx(0.01f));
    CHECK(time.alpha == doctest::Approx(0.5f));
    CHECK(time.fixedTickCount == 2);
    CHECK(time.rawDelta == doctest::Approx(0.05f));
    EngineTime spike{};
    spike.fixedDelta = 0.02f;
    spike.maxAccumulation = 0.1f;
    int burst = 0;
    timeAdvance(spike, 1.0f, [&](float) { burst += 1; });
    CHECK(spike.rawDelta == doctest::Approx(0.1f));
    CHECK(burst == 5);
    CHECK(spike.fixedTickCount == 5);
    CHECK(burst < 50);
}

TEST_CASE("style cascade keeps padding and replaces danger fields") {
    StyleRule rules[2]{};
    rules[0].classHash = uiClassHash("button");
    rules[0].priority = 0;
    rules[0].mask = kUiStyleBg | kUiStylePadding | kUiStyleRadius;
    rules[0].style.bg_color = 0xFF224466u;
    rules[0].style.padding[0] = rules[0].style.padding[1] = rules[0].style.padding[2] = rules[0].style.padding[3] = 8.0f;
    rules[0].style.radius[0] = rules[0].style.radius[1] = rules[0].style.radius[2] = rules[0].style.radius[3] = 4.0f;
    rules[1].classHash = uiClassHash("danger");
    rules[1].priority = 1;
    rules[1].mask = kUiStyleBg | kUiStyleBorderWidth;
    rules[1].style.bg_color = 0xFFAA2222u;
    rules[1].style.border_width = 2.0f;
    CHECK(rules[0].classHash != rules[1].classHash);
    StyleSheet sheet{rules, 2};
    const uint32_t classes[] = {uiClassHash("button"), uiClassHash("danger")};
    UiStyle out{};
    uiResolveStyle(sheet, classes, 2, &out);
    CHECK(out.padding[0] == doctest::Approx(8.0f));
    CHECK(out.padding[3] == doctest::Approx(8.0f));
    CHECK(out.radius[0] == doctest::Approx(4.0f));
    CHECK(out.bg_color == 0xFFAA2222u);
    CHECK(out.border_width == doctest::Approx(2.0f));
    CHECK(out.opacity == doctest::Approx(1.0f));
    YGNodeRef node = YGNodeNew();
    uiStyleApplyYoga(node, out);
    CHECK(YGNodeStyleGetPadding(node, YGEdgeTop).value == doctest::Approx(8.0f));
    CHECK(YGNodeStyleGetPadding(node, YGEdgeLeft).value == doctest::Approx(8.0f));
    YGNodeFree(node);
}

TEST_CASE("input queue tracks pointer keys pad pen touch and tray") {
    InputQueue queue{};
    InputEvent move = inputMake(InputDevice::Mouse, InputEventType::MouseMove);
    move.x = 100.0f;
    move.y = 200.0f;
    InputEvent move2 = move;
    move2.x = 110.0f;
    move2.y = 215.0f;
    InputEvent key = inputMake(InputDevice::Keyboard, InputEventType::KeyDown);
    key.code = kInputKeyA;
    CHECK(inputPushEvent(queue, move));
    CHECK(inputPushEvent(queue, move2));
    CHECK(inputPushEvent(queue, key));
    InputState state{};
    inputProcessQueue(state, queue);
    CHECK(state.mouseX == doctest::Approx(110.0f));
    CHECK(state.mouseY == doctest::Approx(215.0f));
    CHECK(state.deltaX == doctest::Approx(10.0f));
    CHECK(state.deltaY == doctest::Approx(15.0f));
    CHECK(inputKeyDown(state, kInputKeyA));
    CHECK(inputKeyPressed(state, kInputKeyA));
    inputBeginFrame(state);
    CHECK(state.deltaX == doctest::Approx(0.0f));
    CHECK(state.deltaY == doctest::Approx(0.0f));
    CHECK(inputKeyDown(state, kInputKeyA));
    CHECK(!inputKeyPressed(state, kInputKeyA));

    InputEvent button = inputMake(InputDevice::Gamepad, InputEventType::ButtonDown);
    button.deviceId = 0;
    button.code = 1;
    InputEvent stick = inputMake(InputDevice::Gamepad, InputEventType::AxisMotion);
    stick.axisX = 0.5f;
    stick.axisY = -0.25f;
    CHECK(inputPushEvent(queue, button));
    CHECK(inputPushEvent(queue, stick));
    inputProcessQueue(state, queue);
    CHECK((state.pads[0].active & 2u) != 0);
    CHECK((state.pads[0].pressed & 2u) != 0);
    CHECK(state.pads[0].lx == doctest::Approx(0.5f));
    CHECK(state.pads[0].ly == doctest::Approx(-0.25f));

    InputEvent pen = inputMake(InputDevice::TabletPen, InputEventType::TabletPenMove);
    pen.x = 200.0f;
    pen.y = 300.0f;
    pen.pressure = 0.75f;
    pen.tiltX = 0.2f;
    pen.tiltY = -0.1f;
    CHECK(inputPushEvent(queue, pen));
    inputProcessQueue(state, queue);
    CHECK(state.penX == doctest::Approx(200.0f));
    CHECK(state.penY == doctest::Approx(300.0f));
    CHECK(state.penPressure == doctest::Approx(0.75f));
    CHECK(state.penTiltX == doctest::Approx(0.2f));
    CHECK(state.penTiltY == doctest::Approx(-0.1f));
    CHECK(state.penActive == 1);

    InputEvent touch = inputMake(InputDevice::Touch, InputEventType::TouchDown);
    touch.deviceId = 3;
    touch.x = 12.0f;
    touch.y = 40.0f;
    InputEvent tray = inputMake(InputDevice::SystemTray, InputEventType::TrayIconClick);
    tray.code = 7;
    CHECK(inputPushEvent(queue, touch));
    CHECK(inputPushEvent(queue, tray));
    inputProcessQueue(state, queue);
    CHECK(state.touches[0].active == 1);
    CHECK(state.touches[0].pressed == 1);
    CHECK(state.touches[0].id == 3);
    CHECK(state.touches[0].x == doctest::Approx(12.0f));
    CHECK(state.trayClicked == 1);
    CHECK(state.trayCode == 7);
    inputBeginFrame(state);
    CHECK(state.touches[0].active == 1);
    CHECK(state.touches[0].pressed == 0);
    CHECK(state.trayClicked == 0);
    CHECK((state.pads[0].active & 2u) != 0);
    CHECK(state.pads[0].pressed == 0);

    InputQueue full{};
    InputEvent extra = inputMake(InputDevice::Mouse, InputEventType::Scroll);
    for (uint32_t i = 0; i < kInputQueueCap; ++i) {
        CHECK(inputPushEvent(full, extra));
    }
    CHECK(!inputPushEvent(full, extra));
}

TEST_CASE("srgb linear hsv and packed rgba8") {
    ColorRgba8 white{};
    white.rgba = colorPackBytes(255, 255, 255, 255);
    ColorLinear linear = colorSrgb8ToLinear(white);
    CHECK(linear.r == doctest::Approx(1.0f));
    CHECK(linear.g == doctest::Approx(1.0f));
    CHECK(linear.b == doctest::Approx(1.0f));
    CHECK(linear.a == doctest::Approx(1.0f));
    ColorRgba8 back = colorLinearToSrgb8(linear);
    CHECK(back.rgba == white.rgba);
    ColorRgba8 red{};
    red.rgba = colorPackBytes(255, 0, 0, 255);
    CHECK(red.rgba == 0xFF0000FFu);
    ColorLinear mid = colorSrgb8ToLinear(ColorRgba8{colorPackBytes(128, 64, 32, 255)});
    ColorRgba8 round = colorLinearToSrgb8(mid);
    CHECK(colorByte(round.rgba, 0) >= 127);
    CHECK(colorByte(round.rgba, 0) <= 129);
    ColorLinear hsv{};
    colorHsvToLinear(0.0f, 1.0f, 1.0f, hsv);
    CHECK(hsv.r == doctest::Approx(1.0f));
    CHECK(hsv.g == doctest::Approx(0.0f));
    CHECK(hsv.b == doctest::Approx(0.0f));
}

TEST_CASE("document insert delete undo and redo") {
    Document doc{};
    CHECK(docInsert(doc, 0, "Burn", 4));
    CHECK(docInsert(doc, 4, "hope", 4));
    CHECK(doc.length == 8);
    CHECK(std::memcmp(doc.text, "Burnhope", 8) == 0);
    CHECK(docDelete(doc, 4, 4));
    CHECK(doc.length == 4);
    CHECK(std::memcmp(doc.text, "Burn", 4) == 0);
    CHECK(docUndo(doc));
    CHECK(std::memcmp(doc.text, "Burnhope", 8) == 0);
    CHECK(docUndo(doc));
    CHECK(std::memcmp(doc.text, "Burn", 4) == 0);
    CHECK(docRedo(doc));
    CHECK(std::memcmp(doc.text, "Burnhope", 8) == 0);
    CHECK(doc.caret == 8);
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

TEST_CASE("frustum keeps the sphere in front of the camera") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = pose.target[1] = pose.target[2] = 0.0f;
    pose.zn = 0.1f;
    pose.zf = 50.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    InstanceBounds bounds[2]{};
    bounds[0].radius = 0.5f;
    bounds[1].center[2] = 20.0f;
    bounds[1].radius = 0.5f;
    uint32_t visible[2]{};
    const uint32_t n = cullInstances(cam, bounds, 2, visible, 2);
    CHECK(n == 1);
    CHECK(visible[0] == 0);
}

TEST_CASE("visbuffer resolve reads the material by instance id") {
    GpuInstance instance{};
    instanceSet(instance, 0.0f, 0.0f, 0.0f, 1.0f, 2, 0);
    MaterialGpuData materials[3]{};
    materials[2].roughness = 0.3f;
    materials[2].metallic = 0.7f;
    materials[2].baseColor[0] = 0.2f;
    materials[2].baseColor[1] = 0.4f;
    materials[2].baseColor[2] = 0.6f;
    VisHit hit{};
    CHECK(visResolve(visPack(0, 5), &instance, 1, materials, 3, hit));
    CHECK(hit.instanceId == 0);
    CHECK(hit.primitiveId == 5);
    CHECK(hit.materialId == 2);
    CHECK(hit.material.roughness == doctest::Approx(0.3f));
    CHECK(hit.material.metallic == doctest::Approx(0.7f));
    CHECK(hit.material.baseColor[2] == doctest::Approx(0.6f));
}

TEST_CASE("cube meshlet stays inside the vertex and triangle caps") {
    MeshletBlob cube{};
    genCubeMeshlets(1.0f, 1, &cube);
    CHECK(cube.vertexCount == 8);
    CHECK(cube.triangleCount == 12);
    CHECK(cube.vertexCount <= kMeshletMaxVerts);
    CHECK(cube.triangleCount <= kMeshletMaxTris);
    CHECK(cube.meshletCount == 1);
    MeshletBlob plane{};
    genPlaneMeshlets(20.0f, 0, &plane);
    CHECK(plane.vertexCount == 4);
    CHECK(plane.triangleCount == 2);
    CHECK(plane.indices[0] == 0);
    CHECK(plane.indices[1] == 1);
    CHECK(plane.indices[2] == 2);
    CHECK(plane.indices[3] == 2);
    CHECK(plane.indices[4] == 3);
    CHECK(plane.indices[5] == 0);
    CHECK(plane.vertices[0].pos[1] == 0.0f);
    TestScene scene{};
    testSceneBuild(scene);
    CHECK(scene.objectCount == 4);
    CHECK(scene.objects[1].y > 0.6f);
    CHECK(scene.objects[2].y > 0.4f);
    CHECK(scene.objects[3].y > 0.8f);
    CHECK(scene.lightCount == 3);
    ClusterCell cells[kClusterCount]{};
    uint32_t indices[16]{};
    uint32_t indexCount = 0;
    CHECK(clusterAssignLights(cells, indices, 16, indexCount, scene.lights, scene.lightCount));
    const uint32_t a = clusterIndex(2, 1, 3);
    const uint32_t b = clusterIndex(8, 1, 6);
    const uint32_t c = clusterIndex(14, 1, 10);
    CHECK(cells[a].count == 1);
    CHECK(cells[b].count == 1);
    CHECK(cells[c].count == 1);
    CHECK(a != b);
    CHECK(b != c);
    const ScreenBary center = screenBary(1.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 3.0f);
    CHECK(center.flat == false);
    CHECK(center.u == doctest::Approx(1.0f / 3.0f));
    CHECK(center.v == doctest::Approx(1.0f / 3.0f));
    CHECK(center.w == doctest::Approx(1.0f / 3.0f));
    const ScreenBary edge = screenBary(1.5f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 3.0f);
    CHECK(edge.u == doctest::Approx(0.5f));
    CHECK(edge.v == doctest::Approx(0.5f));
    CHECK(edge.w == doctest::Approx(0.0f));
    const ScreenBary outside = screenBary(2.0f, 2.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 3.0f);
    CHECK(outside.u < -0.05f);
    const ScreenBary collapsed = screenBary(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f);
    CHECK(collapsed.flat == true);
    SunShadow sun = sunShadowBuild(cameraCullBuild(CameraPose{}, nullptr), scene.sunDir[0], scene.sunDir[1], scene.sunDir[2]);
    CHECK(sun.resolution == kSunShadowResolution);
    CHECK(sun.viewProj.m[0] != 0.0f);
    uint32_t mip = 0;
    Meshlet facing = scene.cubes[0].meshlets[0];
    facing.center[0] = facing.center[1] = facing.center[2] = 0.0f;
    facing.coneAxis[0] = facing.coneAxis[1] = 0.0f;
    facing.coneAxis[2] = 1.0f;
    facing.coneCutoff = 0.0f;
    facing.radius = 1.0f;
    CameraPose pose{};
    pose.eye[2] = 4.0f;
    CHECK(meshletPassCull(cameraCullBuild(pose, nullptr), facing, 1080.0f, 8, mip));
}

TEST_CASE("sun cascade keeps the slice and reverse depth") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 2.0f;
    pose.eye[2] = 0.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 2.0f;
    pose.target[2] = -8.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    const SunShadow sun = sunShadowSlice(cam, 0.0f, -1.0f, 0.0f, 0.2f, 36.0f, 28.0f, 8.0f);
    const auto project = [&](float x, float y, float z, float& u, float& v, float& depth) {
        const float cx = sun.viewProj.m[0] * x + sun.viewProj.m[4] * y + sun.viewProj.m[8] * z + sun.viewProj.m[12];
        const float cy = sun.viewProj.m[1] * x + sun.viewProj.m[5] * y + sun.viewProj.m[9] * z + sun.viewProj.m[13];
        const float cz = sun.viewProj.m[2] * x + sun.viewProj.m[6] * y + sun.viewProj.m[10] * z + sun.viewProj.m[14];
        const float cw = sun.viewProj.m[3] * x + sun.viewProj.m[7] * y + sun.viewProj.m[11] * z + sun.viewProj.m[15];
        const float w = cw == 0.0f ? 1.0f : cw;
        u = cx / w * 0.5f + 0.5f;
        v = cy / w * 0.5f + 0.5f;
        depth = cz / w;
    };
    float u = 0.0f;
    float v = 0.0f;
    float ground = 0.0f;
    float high = 0.0f;
    project(0.0f, 0.0f, -6.0f, u, v, ground);
    CHECK(u > 0.05f);
    CHECK(u < 0.95f);
    CHECK(v > 0.05f);
    CHECK(v < 0.95f);
    project(0.0f, 8.0f, -6.0f, u, v, high);
    CHECK(high > ground);
    CHECK(sun.radius >= 8.0f);
}

TEST_CASE("look straight down keeps a finite view") {
    CameraPose pose{};
    pose.eye[1] = 8.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 0.0f;
    pose.target[2] = 0.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    CHECK(cam.right[0] == cam.right[0]);
    CHECK(std::fabs(cam.right[0]) > 0.5f);
    CHECK(cam.viewProj.m[0] == cam.viewProj.m[0]);
}

TEST_CASE("meshlet partition stays inside the caps and the bhop header matches") {
    std::vector<float> xyz;
    std::vector<float> nrm;
    std::vector<uint32_t> idx;
    xyz.reserve(500 * 9);
    nrm.reserve(500 * 9);
    idx.reserve(500 * 3);
    for (int i = 0; i < 500; ++i) {
        const float x = static_cast<float>(i);
        const float tri[9] = {x, 0.0f, 0.0f, x + 0.5f, 0.0f, 0.2f, x, 0.0f, 0.4f};
        const float up[9] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (int k = 0; k < 9; ++k) {
            xyz.push_back(tri[k]);
            nrm.push_back(up[k]);
        }
        idx.push_back(static_cast<uint32_t>(i * 3));
        idx.push_back(static_cast<uint32_t>(i * 3 + 1));
        idx.push_back(static_cast<uint32_t>(i * 3 + 2));
    }
    MeshletBuild built{};
    meshletBuildAppend(built, xyz.data(), nrm.data(), idx.data(), static_cast<uint32_t>(idx.size()), 0);
    CHECK(built.meshlets.size() > 1);
    for (const Meshlet& meshlet : built.meshlets) {
        CHECK(meshlet.vertexCount <= kMeshletMaxVerts);
        CHECK(meshlet.triangleCount <= kMeshletMaxTris);
        CHECK(meshlet.vertexCount > 0);
    }
    const float one[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float face[9] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint32_t tri[3] = {0, 1, 2};
    MeshletBuild cone{};
    meshletBuildAppend(cone, one, face, tri, 3, 1);
    CHECK(cone.meshlets.size() == 1);
    const float ax = cone.meshlets[0].center[0];
    const float az = cone.meshlets[0].center[2];
    CHECK(meshletConeVisible(cone.meshlets[0], ax, 5.0f, az));
    CHECK(!meshletConeVisible(cone.meshlets[0], ax, -5.0f, az));
    const char* path = "/tmp/burnhope-meshlet.bhop";
    CHECK(sceneBlobWrite(path, cone.meshlets.data(), 1, cone.vertices.data(), static_cast<uint32_t>(cone.vertices.size()), cone.indices.data(), static_cast<uint32_t>(cone.indices.size()), cone.instances.data(), 1));
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    SceneBlobView view{};
    CHECK(sceneBlobView(mapped.data, mapped.size, &view));
    CHECK(view.info->meshletCount == 1);
    CHECK(view.info->instanceCount == 1);
    CHECK(view.meshlets[0].triangleCount == 1);
    fileUnmap(&mapped);
}

TEST_CASE("a sector stays visible until a portal is marked and leaves the frustum") {
    Sector open{};
    CHECK(sectorVisible(open, nullptr, 0, nullptr));
    Portal door{};
    door.center[0] = 0.0f;
    door.radius = 0.5f;
    const float plane[24] = {
        1.0f, 0.0f, 0.0f, -5.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    Sector hall{};
    hall.portalCount = 1;
    CHECK(!sectorVisible(hall, &door, 1, plane));
    door.center[0] = 10.0f;
    CHECK(sectorVisible(hall, &door, 1, plane));
}

TEST_CASE("stream ring wraps after the first chunks are released") {
    alignas(16) static uint8_t storage[4 * 65536];
    StreamStagingRing ring{};
    streamRingInit(ring, storage, sizeof(storage), 65536);
    uint32_t off[4]{};
    uint8_t* ptr[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        ptr[i] = streamRingAlloc(ring, 65536, &off[i]);
        CHECK(ptr[i] != nullptr);
        CHECK(off[i] == i * 65536);
    }
    CHECK(ring.freeChunks == 0);
    CHECK(streamRingAlloc(ring, 65536, &off[0]) == nullptr);
    streamRingRelease(ring, 0, 65536);
    streamRingRelease(ring, 65536, 65536);
    CHECK(ring.freeChunks == 2);
    uint32_t wrapped = 99;
    uint8_t* again = streamRingAlloc(ring, 65536, &wrapped);
    CHECK(again == storage);
    CHECK(wrapped == 0);
}

TEST_CASE("stream queue picks the nearest asset") {
    StreamQueue queue{};
    StreamRequest far{};
    far.assetHash = 3;
    far.priority = 12.0f;
    StreamRequest mid{};
    mid.assetHash = 2;
    mid.priority = 4.0f;
    StreamRequest nearAsset{};
    nearAsset.assetHash = 1;
    nearAsset.priority = 0.5f;
    CHECK(streamQueuePush(queue, far));
    CHECK(streamQueuePush(queue, mid));
    CHECK(streamQueuePush(queue, nearAsset));
    const int32_t pick = streamQueuePick(queue);
    CHECK(pick == 2);
    CHECK(queue.requests[pick].assetHash == 1);
}

TEST_CASE("stream commit marks the mapped blob resident") {
    const char* path = "/tmp/burnhope-stream.bin";
    const uint8_t body[4] = {1, 2, 3, 4};
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(body, 1, 4, file) == 4);
    std::fclose(file);
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    alignas(16) uint8_t staging[65536];
    StreamStagingRing ring{};
    streamRingInit(ring, staging, sizeof(staging), 65536);
    uint32_t offset = 0;
    uint8_t* dst = streamRingAlloc(ring, static_cast<uint32_t>(mapped.size), &offset);
    CHECK(dst != nullptr);
    std::memcpy(dst, mapped.data, mapped.size);
    CHECK(std::memcmp(dst, body, 4) == 0);
    StreamRequest request{};
    request.assetHash = assetHash("stream-blob");
    request.byteSize = static_cast<uint32_t>(mapped.size);
    request.gpuTargetAddress = 0x1000;
    request.status = kStreamInFlight;
    ResidencyTable table{};
    CHECK(streamCommitResident(request, table));
    CHECK(request.status == kStreamComplete);
    ResidencySlot* slot = residencyFind(table, request.assetHash, false);
    CHECK(slot != nullptr);
    CHECK(slot->state == kResidencyResident);
    fileUnmap(&mapped);
    streamRingRelease(ring, offset, request.byteSize);
}

TEST_CASE("fly camera moves forward and yaws with the mouse") {
    FlyCam cam{};
    const float z0 = cam.eye[2];
    InputState input{};
    inputSetBit(input.keyActive, kFlyKeyW);
    flyCamUpdate(cam, input, 1.0f);
    CHECK(cam.eye[2] < z0);
    input = {};
    input.mouseActive = 2;
    input.deltaX = 100.0f;
    input.deltaY = -50.0f;
    const float yaw0 = cam.yaw;
    const float pitch0 = cam.pitch;
    flyCamUpdate(cam, input, 0.016f);
    CHECK(cam.yaw > yaw0);
    CHECK(cam.pitch > pitch0);
    CHECK(cam.pitch < 1.56f);
    const float y0 = cam.eye[1];
    const float z1 = cam.eye[2];
    input = {};
    inputSetBit(input.keyActive, kFlyKeySpace);
    flyCamUpdate(cam, input, 1.0f);
    CHECK(cam.eye[1] > y0);
    CHECK(cam.eye[2] == doctest::Approx(z1));
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    flyCamForward(cam, tx, ty, tz);
    CHECK(ty > 0.0f);
    const float yawBefore = cam.yaw;
    input = {};
    input.mouseActive = 2;
    input.deltaX = 4000.0f;
    flyCamUpdate(cam, input, 0.016f);
    CHECK(cam.yaw - yawBefore == doctest::Approx(240.0f * 0.004f));
}

TEST_CASE("point light shadow face and atlas index") {
    LightGpuData lights[2]{};
    lights[0].type = kLightPoint;
    lights[0].pos[0] = 2.0f;
    lights[0].radius = 4.0f;
    lights[1].type = kLightDirectional;
    pointShadowAssign(lights, 2);
    CHECK(lights[0].shadowMapIndex == 0);
    CHECK(lights[1].shadowMapIndex == -1);
    const Mat4 face = pointShadowFace(lights[0], 0);
    CHECK(face.m[2] != 0.0f);
    const Mat4 upFace = pointShadowFace(lights[0], 2);
    CHECK(upFace.m[0] == upFace.m[0]);
    CHECK(pointShadowDepth(6.0f, 0.0f, 0.0f, lights[0]) == doctest::Approx(1.0f));
}

TEST_CASE("meshlet cone culls a sphere that faces away") {
    MeshletVertex verts[4]{};
    verts[0].pos[2] = 1.0f;
    verts[1].pos[0] = 0.4f;
    verts[1].pos[2] = 0.8f;
    verts[2].pos[0] = -0.4f;
    verts[2].pos[2] = 0.8f;
    verts[3].pos[1] = 0.4f;
    verts[3].pos[2] = 0.8f;
    for (MeshletVertex& v : verts) {
        v.packedNormal = meshletPackNormal(v.pos[0], v.pos[1], v.pos[2]);
    }
    Meshlet meshlet{};
    meshletFit(meshlet, verts, 4);
    CHECK(meshlet.radius > 0.0f);
    CHECK(meshlet.coneAxis[2] > 0.5f);
    CHECK(meshletConeVisible(meshlet, 0.0f, 0.0f, 4.0f));
    CHECK(!meshletConeVisible(meshlet, 0.0f, 0.0f, -4.0f));
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = pose.target[1] = pose.target[2] = 0.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    CHECK(cameraConeVisible(cam, meshlet.center[0], meshlet.center[1], meshlet.center[2], meshlet.coneAxis[0], meshlet.coneAxis[1], meshlet.coneAxis[2], meshlet.coneCutoff));
    CHECK(hizMipForSphere(1.0f, 10.0f, 1080.0f, 1.0f, 8) >= 6);
    const float mip[4] = {0.4f, 0.4f, 0.4f, 0.4f};
    CHECK(hizSphereVisible(mip, 2, 2, 0.5f, 0.5f, 0.2f));
    CHECK(!hizSphereVisible(mip, 2, 2, 0.5f, 0.5f, 0.9f));
    CHECK(assetHash("MeshletBlob") == uiClassHash("MeshletBlob"));
}

TEST_CASE("cluster grid keeps a point light in its sphere") {
    LightGpuData light{};
    light.type = kLightPoint;
    light.pos[0] = 8.5f;
    light.pos[1] = 4.5f;
    light.pos[2] = 12.5f;
    light.radius = 0.25f;
    ClusterCell cells[kClusterCount]{};
    uint32_t indices[8]{};
    uint32_t indexCount = 0;
    CHECK(clusterAssignLights(cells, indices, 8, indexCount, &light, 1));
    const uint32_t hit = clusterIndex(8, 4, 12);
    CHECK(cells[hit].count == 1);
    CHECK(indices[cells[hit].offset] == 0);
    CHECK(cells[clusterIndex(0, 0, 0)].count == 0);
    CHECK(indexCount == 1);
}

TEST_CASE("meshlet blob maps without a copy") {
    Meshlet meshlet{};
    meshlet.center[0] = 1.0f;
    meshlet.radius = 2.0f;
    meshlet.vertexCount = 3;
    uint8_t blob[sizeof(AssetHeader) + sizeof(Meshlet)]{};
    const size_t bytes = meshletSerialize(&meshlet, 1, blob, sizeof(blob));
    CHECK(bytes == sizeof(blob));
    const char* path = "/tmp/burnhope-meshlet.bin";
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(blob, 1, bytes, file) == bytes);
    std::fclose(file);
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    uint32_t count = 0;
    const Meshlet* view = meshletView(mapped.data, mapped.size, count);
    CHECK(view != nullptr);
    CHECK(count == 1);
    CHECK(view->radius == doctest::Approx(2.0f));
    CHECK(view->vertexCount == 3);
    CHECK(reinterpret_cast<const uint8_t*>(view) == mapped.data + sizeof(AssetHeader));
    fileUnmap(&mapped);
    std::remove(path);
}

TEST_CASE("mapped file reads the bytes that were written") {
    const char* path = "/tmp/burnhope-map-test.bin";
    const char bytes[] = "BHOP-map";
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(bytes, 1, 8, file) == 8);
    std::fclose(file);
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    CHECK(mapped.size == 8);
    CHECK(mapped.data != nullptr);
    CHECK(std::memcmp(mapped.data, bytes, 8) == 0);
    fileUnmap(&mapped);
    CHECK(mapped.data == nullptr);
    CHECK(mapped.fd < 0);
    std::remove(path);
}

TEST_CASE("material blob roundtrips through the asset header") {
    MaterialGpuData mat{};
    mat.baseColor[0] = 0.2f;
    mat.baseColor[1] = 0.4f;
    mat.baseColor[2] = 0.6f;
    mat.roughness = 0.35f;
    mat.metallic = 0.8f;
    mat.albedoTexIndex = 3;
    uint8_t blob[sizeof(AssetHeader) + sizeof(MaterialGpuData)]{};
    const size_t bytes = materialSerialize(mat, blob, sizeof(blob));
    CHECK(bytes == sizeof(blob));
    AssetHeader header{};
    std::memcpy(&header, blob, sizeof(header));
    CHECK(header.magic == kAssetMagic);
    CHECK(header.version == 1);
    CHECK(header.typeHash == assetHash("Material"));
    CHECK(header.dataSize == sizeof(MaterialGpuData));
    MaterialGpuData back{};
    CHECK(materialDeserialize(blob, bytes, &back));
    CHECK(std::memcmp(&mat, &back, sizeof(mat)) == 0);
    blob[sizeof(AssetHeader)] ^= 1;
    CHECK(!materialDeserialize(blob, bytes, &back));
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

int32_t wasmTick(void*, const int32_t*, uint32_t) {
    return 1;
}

TEST_CASE("wasm add and apply through linear memory") {
    constexpr uint8_t kWasm[] = {
        0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,0x01,0x10,0x03,0x60,0x00,0x01,0x7f,0x60,0x02,0x7f,0x7f,0x01,0x7f,0x60,0x01,0x7f,0x01,0x7f,
        0x02,0x0c,0x01,0x03,0x65,0x6e,0x76,0x04,0x74,0x69,0x63,0x6b,0x00,0x00,0x03,0x03,0x02,0x01,0x02,0x05,0x03,0x01,0x00,0x01,
        0x07,0x15,0x03,0x03,0x61,0x64,0x64,0x00,0x01,0x05,0x61,0x70,0x70,0x6c,0x79,0x00,0x02,0x03,0x6d,0x65,0x6d,0x02,0x00,
        0x0a,0x1e,0x02,0x07,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b,0x14,0x00,0x20,0x00,0x20,0x00,0x28,0x02,0x00,0x10,0x00,0x6a,0x36,0x02,0x00,0x20,0x00,0x28,0x02,0x00,0x0b,
    };
    FrameArena arena{};
    CHECK(frameArenaCreate(arena, 128 * 1024));
    WasmHost host{};
    CHECK(wasmBind(host, "env", "tick", wasmTick, nullptr));
    CHECK(wasmLoad(host, kWasm, static_cast<uint32_t>(sizeof(kWasm)), arena));
    const int32_t args[2] = {15, 27};
    int32_t result = 0;
    CHECK(wasmInvoke(host, "add", args, 2, result) == WasmStatus::Ok);
    CHECK(result == 42);
    uint32_t memBytes = 0;
    uint8_t* mem = wasmGetMemory(&host, &memBytes);
    CHECK(mem != nullptr);
    CHECK(memBytes == 65536);
    const int32_t start = 15;
    std::memcpy(mem, &start, sizeof(start));
    const int32_t offset = 0;
    CHECK(wasmInvoke(host, "apply", &offset, 1, result) == WasmStatus::Ok);
    CHECK(result == 16);
    int32_t stored = 0;
    std::memcpy(&stored, mem, sizeof(stored));
    CHECK(stored == 16);
    const int32_t far = 70000;
    CHECK(wasmInvoke(host, "apply", &far, 1, result) == WasmStatus::Trap);
    std::memcpy(&stored, mem, sizeof(stored));
    CHECK(stored == 16);
    frameArenaDestroy(arena);
}

TEST_CASE("photo blur packs one lod into the photo primitive") {
    UiState state;
    const uint16_t id = uiIcon(state, kUiNone, 0, 64.0f, 64.0f);
    uiLayout(state, 64.0f, 64.0f);
    Canvas canvas;
    canvas.state = &state;
    canvasBlur(canvas, id, 1.5f);
    CHECK(state.ent[id].try_get<UiPaint>()->blur == doctest::Approx(1.0f));
    std::vector<UiPrimitive> prims(kUiCap);
    const uint32_t n = uiEmit(state, prims.data(), static_cast<uint32_t>(prims.size()));
    int photos = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if ((prims[i].flags & kUiFlagPhoto) == 0) {
            continue;
        }
        ++photos;
        float blur = 0.0f;
        std::memcpy(&blur, &prims[i].pad1, sizeof(float));
        CHECK(blur == doctest::Approx(1.0f));
        CHECK((prims[i].flags & kUiFlagTransform) == 0);
    }
    CHECK(photos == 1);
    CHECK(uiPhotoLod(0.0f, 8) == doctest::Approx(0.0f));
    CHECK(uiPhotoLod(0.5f, 8) == doctest::Approx(3.5f));
    CHECK(uiPhotoLod(2.0f, 1) == doctest::Approx(0.0f));
}

TEST_CASE("panel transform/filter sets the flag on the filled primitive") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(200, 80), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    uiLayout(state, 200.0f, 80.0f);
    Canvas canvas;
    canvas.state = &state;
    canvasTransform(canvas, root, 30.0f, 1.2f, 1.0f);
    canvasFilter(canvas, root, 1.3f, 0.7f);
    std::vector<UiPrimitive> prims(kUiCap);
    const uint32_t n = uiEmit(state, prims.data(), static_cast<uint32_t>(prims.size()));
    CHECK(n > 0);
    bool found = false;
    for (uint32_t i = 0; i < n; ++i) {
        if ((prims[i].flags & kUiFlagTransform) != 0) {
            found = true;
            float angle = 0, sx = 0, sy = 0, br = 0, ct = 0;
            uiUnpackTransform(prims[i].pad0, prims[i].pad1, angle, sx, sy, br, ct);
            CHECK(angle == doctest::Approx(30.0f).epsilon(0.5));
            CHECK(sx == doctest::Approx(1.2f).epsilon(0.01));
            CHECK(br == doctest::Approx(1.3f).epsilon(0.02));
            CHECK(ct == doctest::Approx(0.7f).epsilon(0.02));
        }
    }
    CHECK(found);
}

static int rgbaMiss(const uint8_t* a, const uint8_t* b, int pixels, int tol) {
    int miss = 0;
    for (int i = 0; i < pixels * 4; ++i) {
        const int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        if (d > tol) {
            ++miss;
        }
    }
    return miss;
}

TEST_CASE("drop frees the subtree") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(200, 80), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t child = spawn(state, root, px(40, 20), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    const uint16_t label = state.labelOf[child];
    uiLayout(state, 200.0f, 80.0f);
    CHECK(state.yoga[child] != nullptr);
    uiDrop(state, child);
    CHECK(state.yoga[child] == nullptr);
    CHECK(state.yoga[label] == nullptr);
    CHECK(!state.ent[child].is_alive());
    CHECK(!state.ent[label].is_alive());
    CHECK(state.ent[root].is_alive());
    CHECK(YGNodeGetChildCount(state.yoga[root]) == 0);
    uiLayout(state, 200.0f, 80.0f);
    CHECK(hitTest(state, 10.0f, 10.0f) != child);
}

TEST_CASE("reparent moves the yoga parent") {
    UiState state;
    const uint16_t host = spawn(state, kUiNone, px(220, 40), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    const uint16_t a = spawn(state, host, px(100, 40), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    const uint16_t b = spawn(state, host, px(100, 40), paint(UiRole::Panel, 0.2f, 0.2f, 0.2f));
    const uint16_t child = spawn(state, a, px(20, 10), paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    uiReparent(state, child, b);
    CHECK(state.parentOf[child] == b);
    CHECK(YGNodeGetParent(state.yoga[child]) == state.yoga[b]);
    uiLayout(state, 220.0f, 40.0f);
    CHECK(state.box[child].w == doctest::Approx(20.0f));
}

TEST_CASE("hidden node is not a hit") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(80, 30), paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
    uiShow(state, id, false);
    uiLayout(state, 200.0f, 80.0f);
    CHECK(YGNodeStyleGetDisplay(state.yoga[id]) == YGDisplayNone);
    CHECK(hitTest(state, 10.0f, 10.0f) == kUiNone);
}

TEST_CASE("scissor edge and a nested child") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(200, 80), paint(UiRole::Panel, 0.05f, 0.05f, 0.05f));
    UiFlex scrollFlex = px(100, 40);
    scrollFlex.overflow = 2;
    const uint16_t scroll = spawn(state, root, scrollFlex, paint(UiRole::Scroll, 0.1f, 0.1f, 0.1f));
    UiFlex deep = px(80, 80);
    deep.position = 1;
    deep.posY = 0.0f;
    deep.overflow = 2;
    const uint16_t inner = spawn(state, scroll, deep, paint(UiRole::Scroll, 0.05f, 0.05f, 0.05f));
    UiFlex buried = px(16, 16);
    buried.position = 1;
    buried.posY = 50.0f;
    const uint16_t buriedBtn = spawn(state, inner, buried, paint(UiRole::Button, 0.4f, 0.2f, 0.2f));
    UiFlex child = px(30, 16);
    child.position = 1;
    child.posX = 10.0f;
    child.posY = 8.0f;
    const uint16_t button = spawn(state, scroll, child, paint(UiRole::Button, 0.3f, 0.3f, 0.3f));
    uiLayout(state, 200.0f, 80.0f);
    const UiBox& box = state.box[button];
    CHECK(hitTest(state, box.x + 1.0f, box.y + 1.0f) == button);
    CHECK(hitTest(state, box.x - 1.0f, box.y + 1.0f) != button);
    CHECK(hitTest(state, box.x + box.w, box.y + 1.0f) != button);
    CHECK(hitTest(state, box.x + box.w - 1.0f, box.y + 1.0f) == button);
    const UiBox& deepBox = state.box[buriedBtn];
    CHECK(deepBox.h > 1.0f);
    CHECK(hitTest(state, deepBox.x + 1.0f, deepBox.y + 1.0f) != buriedBtn);
}

TEST_CASE("broken blobs do not import") {
    unsigned char junk[64];
    for (int i = 0; i < 64; ++i) {
        junk[i] = static_cast<unsigned char>(i * 17 + 3);
    }
    UiState state;
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
    named.role = static_cast<uint8_t>(UiRole::Panel);
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
    uiApplyInput(state, in);
    const UiRange* after = state.ent[scale].try_get<UiRange>();
    CHECK(after->value == doctest::Approx(before->min + 0.8f * (before->max - before->min)).epsilon(0.08));
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

TEST_CASE("file new folder and zoom slider") {
    namespace fs = std::filesystem;
    const fs::path dir = "/tmp/burnhope-folder-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directory(dir, ec);
    UiState state;
    const uint16_t frame = spawn(state, kUiNone, px(900, 640), paint(UiRole::Panel, 0.08f, 0.08f, 0.1f));
    state.fileFrame = frame;
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
        if (paint->role == static_cast<uint8_t>(UiRole::Button) && text != nullptr && std::strcmp(text->bytes, "NEW") == 0) {
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

TEST_CASE("rgba compare tolerates half a percent") {
    uint8_t a[8] = {10, 20, 30, 255, 0, 0, 0, 255};
    uint8_t b[8] = {10, 20, 30, 255, 0, 0, 0, 255};
    CHECK(rgbaMiss(a, b, 2, 2) == 0);
    b[0] = 40;
    CHECK(rgbaMiss(a, b, 2, 2) == 1);
}

// Бенчмарк Фазы 0 плана рендера: не проверяет число (машины разные), только
// печатает время в лог, чтобы было с чем сравнивать после оптимизаций DOD/Flecs.
TEST_CASE("uiLayout timing on a near-cap tree") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(1600, 1000), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex grid{};
    grid.direction = 1; // row
    grid.wrap = 1;
    grid.widthMode = static_cast<uint8_t>(UiSize::Px);
    grid.heightMode = static_cast<uint8_t>(UiSize::Px);
    grid.width = 40.0f;
    grid.height = 24.0f;
    grid.shrink = 0.0f;
    uint16_t made = 0;
    for (uint16_t i = 0; i < 900 && state.count < kUiCap - 4; ++i) {
        const uint16_t id = spawn(state, root, grid, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
        if (id == kUiNone) {
            break;
        }
        ++made;
    }
    const auto t0 = std::chrono::steady_clock::now();
    uiLayout(state, 1600.0f, 1000.0f);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    spdlog::info("bench uiLayout nodes={} ms={:.3f}", static_cast<unsigned>(made), ms);
    CHECK(made > 500);
    CHECK(state.box[root].w == doctest::Approx(1600.0f));
}

namespace {

// Один панель на весь кадр, без радиуса (нет SDF-спада по краю) — внутренние пиксели обязаны
// быть точным цветом заливки, без допуска на антиалиасинг. Фиксированный тестовый экран,
// не зависит ни от одного ассета на диске.
constexpr float kGoldenR = 0.76f;
constexpr float kGoldenG = 0.30f;
constexpr float kGoldenB = 0.10f;

void goldenBuilder(Canvas& c, void*) {
    spawn(*c.state, kUiNone, px(64.0f, 64.0f), paint(UiRole::Panel, kGoldenR, kGoldenG, kGoldenB));
}

// Рендерит текущее содержимое canvas в offscreen-картинку W×H и возвращает RGBA байты через
// гарантированный путь (staging-буфер + vkCmdCopyImageToBuffer), не зависящий от
// host_image_copy — это инфраструктура теста, а не то, что тест проверяет.
// forceVertexPull=true временно гасит mesh-shader путь, чтобы сравнить оба пути один к одному.
std::vector<uint8_t> renderGoldenToRgba(Device& d, Canvas& c, DescriptorHeaps& heaps, uint32_t w, uint32_t h, bool forceVertexPull) {
    std::vector<uint8_t> out;
    GpuImage target{};
    if (!gpuImageCreate(
            target,
            d,
            VkExtent2D{w, h},
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1)) {
        return out;
    }
    GpuBuffer staging{};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4u;
    if (!gpuBufferCreate(staging, d, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        gpuImageDestroy(target, d);
        return out;
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = d.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(d.device, &alloc, &cmd) != VK_SUCCESS) {
        gpuBufferDestroy(staging, d);
        gpuImageDestroy(target, d);
        return out;
    }
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);
    heapBind(cmd, heaps);

    Swapchain fakeSc{};
    fakeSc.images[0] = target.image;
    fakeSc.views[0] = target.view;
    fakeSc.extent = VkExtent2D{w, h};
    fakeSc.asked.transparent = false;
    FrameContext fc{cmd, 0, 0};

    const ShaderExt savedMs = c.ms;
    if (forceVertexPull) {
        c.ms = {};
    }
    canvasRecord(cmd, c, fakeSc, fc);
    if (forceVertexPull) {
        c.ms = savedMs;
    }

    rhiImageBarrier(
        cmd,
        target.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    const VkCommandBufferSubmitInfo cbsi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cbsi,
    };
    if (vkQueueSubmit2(d.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
        vkQueueWaitIdle(d.graphicsQueue);
        out.resize(static_cast<size_t>(bytes));
        std::memcpy(out.data(), staging.mapped, static_cast<size_t>(bytes));
    }
    vkFreeCommandBuffers(d.device, d.commandPool, 1, &cmd);
    gpuBufferDestroy(staging, d);
    gpuImageDestroy(target, d);
    return out;
}

} // namespace

// Фаза 2/3 (план рендера): golden-image инфраструктура, которой до сих пор не было
// (docs/open.md). Offscreen render, не свопчейн — surface не нужен, только устройство.
// Проверяет две вещи разом: (1) заливка панели даёт точный RGBA без допуска, то есть сама
// GPU-труба (шейдер, блендинг, барьеры) не портит цвет; (2) mesh-shader путь (uiMs) и
// vertex-pull путь (uiVs) дают пиксель-в-пиксель одинаковый результат — это и есть риск,
// который Фаза 1 добавила (два способа подать один draw), и до этого теста его никто не ловил.
TEST_CASE("golden image: panel fill is exact and mesh/vertex paths match") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for golden-image test, skip");
        return;
    }
    Device device{};
    if (!deviceCreate(device, window)) {
        spdlog::warn("no Vulkan device for golden-image test, skip");
        windowDestroy(window);
        return;
    }
    DescriptorHeaps heaps{};
    const HeapLayout heapLayout{.buffers = 1, .images = 9, .samplers = 1};
    Canvas canvas{};
    if (!descriptorHeapsCreate(heaps, device, heapLayout) || !canvasCreate(canvas, device, heaps)) {
        spdlog::warn("canvas/heap setup failed, skip golden-image test");
        descriptorHeapsDestroy(heaps, device);
        deviceDestroy(device);
        windowDestroy(window);
        return;
    }
    canvasSetBuilder(canvas, goldenBuilder, nullptr);
    canvasReload(canvas);
    InputFrame input{};
    (void)canvasConsume(canvas, device, 64.0f, 64.0f, input, 0.0);

    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    const std::vector<uint8_t> meshPixels = canvas.ms.handle != VK_NULL_HANDLE
        ? renderGoldenToRgba(device, canvas, heaps, kW, kH, false)
        : std::vector<uint8_t>{};
    const std::vector<uint8_t> vertPixels = renderGoldenToRgba(device, canvas, heaps, kW, kH, true);

    CHECK(vertPixels.size() == static_cast<size_t>(kW) * kH * 4u);
    if (vertPixels.size() == static_cast<size_t>(kW) * kH * 4u) {
        const uint8_t* center = vertPixels.data() + (static_cast<size_t>(kH / 2) * kW + kW / 2) * 4u;
        const uint8_t expected[4] = {
            static_cast<uint8_t>(kGoldenR * 255.0f + 0.5f),
            static_cast<uint8_t>(kGoldenG * 255.0f + 0.5f),
            static_cast<uint8_t>(kGoldenB * 255.0f + 0.5f),
            255,
        };
        spdlog::info("golden vertex-pull center rgba = {} {} {} {}", center[0], center[1], center[2], center[3]);
        CHECK(rgbaMiss(center, expected, 1, 2) == 0);
    }
    if (!meshPixels.empty()) {
        CHECK(meshPixels.size() == vertPixels.size());
        if (meshPixels.size() == vertPixels.size()) {
            const int miss = rgbaMiss(meshPixels.data(), vertPixels.data(), static_cast<int>(kW * kH), 0);
            spdlog::info("golden mesh-vs-vertex mismatched channels = {}", miss);
            CHECK(miss == 0);
        }
    } else {
        spdlog::warn("mesh-shader path unavailable on this GPU, skipped cross-check");
    }

    canvasDestroy(canvas, device);
    descriptorHeapsDestroy(heaps, device);
    deviceDestroy(device);
    windowDestroy(window);
}

// Фаза 3 (план рендера): DOD/Flecs аудит горячего пути отрисовки — вторая половина кадра
// после uiLayout. Проверка чтением кода (Canvas.cpp, Tree.cpp): hitTest и uiEmit уже SoA,
// без std::vector/malloc в кадре — ids[kUiCap] на стеке, сортировка вставкой по уже почти
// отсортированному порядку спавна (O(n) на практике, не O(n^2)). Это измерение фиксирует
// число, чтобы будущая правка цикла не могла тихо откатить это к худшему без сравнения.
TEST_CASE("uiEmit timing on a near-cap tree") {
    UiState state;
    const uint16_t root = spawn(state, kUiNone, px(1600, 1000), paint(UiRole::Panel, 0.1f, 0.1f, 0.1f));
    UiFlex grid{};
    grid.direction = 1;
    grid.wrap = 1;
    grid.widthMode = static_cast<uint8_t>(UiSize::Px);
    grid.heightMode = static_cast<uint8_t>(UiSize::Px);
    grid.width = 40.0f;
    grid.height = 24.0f;
    grid.shrink = 0.0f;
    uint16_t made = 0;
    for (uint16_t i = 0; i < 900 && state.count < kUiCap - 4; ++i) {
        const uint16_t id = spawn(state, root, grid, paint(UiRole::Button, 0.2f, 0.2f, 0.2f));
        if (id == kUiNone) {
            break;
        }
        ++made;
    }
    uiLayout(state, 1600.0f, 1000.0f);
    std::vector<UiPrimitive> prims(kUiPrimCap);
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t n = uiEmit(state, prims.data(), kUiPrimCap);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    spdlog::info("bench uiEmit nodes={} prims={} ms={:.3f}", static_cast<unsigned>(made), n, ms);
    CHECK(n > 0);
}

namespace {

void decodeBc1Block(const uint8_t* block, uint8_t out[16 * 4]) {
    const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
    const uint32_t idx = static_cast<uint32_t>(block[4]) | (static_cast<uint32_t>(block[5]) << 8)
        | (static_cast<uint32_t>(block[6]) << 16) | (static_cast<uint32_t>(block[7]) << 24);
    auto unpack565 = [](uint16_t v, uint8_t rgb[3]) {
        rgb[0] = static_cast<uint8_t>(((v >> 11) & 0x1fu) * 255u / 31u);
        rgb[1] = static_cast<uint8_t>(((v >> 5) & 0x3fu) * 255u / 63u);
        rgb[2] = static_cast<uint8_t>((v & 0x1fu) * 255u / 31u);
    };
    uint8_t col[4][3];
    unpack565(c0, col[0]);
    unpack565(c1, col[1]);
    // Шейдер (core/image/compress.slang) гарантирует c0 > c1 как uint16 — всегда 4-цветный
    // непрозрачный режим BC1, никогда 3-цветный с "прозрачным чёрным".
    for (int ch = 0; ch < 3; ++ch) {
        col[2][ch] = static_cast<uint8_t>((2u * col[0][ch] + col[1][ch]) / 3u);
        col[3][ch] = static_cast<uint8_t>((col[0][ch] + 2u * col[1][ch]) / 3u);
    }
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t sel = (idx >> (i * 2u)) & 3u;
        out[i * 4 + 0] = col[sel][0];
        out[i * 4 + 1] = col[sel][1];
        out[i * 4 + 2] = col[sel][2];
        out[i * 4 + 3] = 255;
    }
}

uint32_t bc7Bits(const uint8_t* block, uint32_t& pos, uint32_t n) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t byte = pos >> 3;
        const uint32_t bit = pos & 7u;
        v |= ((block[byte] >> bit) & 1u) << i;
        ++pos;
    }
    return v;
}

bool decodeBc7Mode6(const uint8_t* block, uint8_t out[16 * 4]) {
    uint32_t pos = 0;
    uint32_t zeros = 0;
    while (pos < 8 && ((block[0] >> pos) & 1u) == 0) {
        ++zeros;
        ++pos;
    }
    if (zeros != 6 || pos >= 128) {
        return false;
    }
    ++pos;
    int ep[2][4];
    for (int c = 0; c < 4; ++c) {
        ep[0][c] = static_cast<int>(bc7Bits(block, pos, 7));
        ep[1][c] = static_cast<int>(bc7Bits(block, pos, 7));
    }
    const int p0 = static_cast<int>(bc7Bits(block, pos, 1));
    const int p1 = static_cast<int>(bc7Bits(block, pos, 1));
    for (int c = 0; c < 4; ++c) {
        ep[0][c] = (ep[0][c] << 1) | p0;
        ep[1][c] = (ep[1][c] << 1) | p1;
    }
    static constexpr int kW[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t n = i == 0 ? 3u : 4u;
        const uint32_t sel = bc7Bits(block, pos, n);
        if (sel > 15) {
            return false;
        }
        const int w = kW[sel];
        for (int c = 0; c < 4; ++c) {
            const int rec = ((64 - w) * ep[0][c] + w * ep[1][c] + 32) >> 6;
            out[i * 4 + c] = static_cast<uint8_t>(rec < 0 ? 0 : (rec > 255 ? 255 : rec));
        }
    }
    return pos == 128;
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1fu;
    const uint32_t mant = h & 0x3ffu;
    uint32_t bits = sign;
    if (exp == 31u) {
        bits |= 0x7f800000u | (mant << 13);
    } else if (exp != 0u) {
        bits |= ((exp + 127u - 15u) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

int unquant10(uint32_t q) {
    if (q == 0u) {
        return 0;
    }
    if (q >= 1023u) {
        return 0xffff;
    }
    return static_cast<int>(((q << 16) + 0x8000u) >> 10);
}

bool decodeBc6hMode11(const uint8_t* block, float out[16 * 3]) {
    uint32_t pos = 0;
    if (bc7Bits(block, pos, 5) != 0x03u) {
        return false;
    }
    int ep[2][3];
    for (int c = 0; c < 3; ++c) {
        ep[0][c] = unquant10(bc7Bits(block, pos, 10));
    }
    for (int c = 0; c < 3; ++c) {
        ep[1][c] = unquant10(bc7Bits(block, pos, 10));
    }
    static constexpr int kW[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t sel = bc7Bits(block, pos, i == 0 ? 3u : 4u);
        if (sel > 15) {
            return false;
        }
        const int w = kW[sel];
        for (int c = 0; c < 3; ++c) {
            const int interp = ((64 - w) * ep[0][c] + w * ep[1][c] + 32) >> 6;
            const int half = (interp * 31) >> 6;
            out[i * 3 + c] = halfToFloat(static_cast<uint16_t>(half < 0 ? 0 : half));
        }
    }
    return pos == 128;
}

void decodeBc4(const uint8_t* block, uint8_t out[16]) {
    const int e0 = block[0];
    const int e1 = block[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    }
    int pal[8];
    pal[0] = e0;
    pal[1] = e1;
    for (int k = 0; k < 6; ++k) {
        pal[k + 2] = ((6 - k) * e0 + (k + 1) * e1) / 7;
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = static_cast<uint8_t>(pal[(bits >> (i * 3)) & 7u]);
    }
}

} // namespace

// BC3/BC4/BC5 — материалы: альфа, roughness, нормали. gpuCompressLevel, не загрузка в свопчейн.
TEST_CASE("gpuCompressLevel: BC3 BC4 BC5 decode close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for BC3/4/5 test, skip");
        return;
    }
    Device device{};
    if (!deviceCreate(device, window)) {
        spdlog::warn("no Vulkan device for BC3/4/5 test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 32;
    constexpr uint32_t kH = 32;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x * 8);
            p[1] = static_cast<uint8_t>(y * 8);
            p[2] = static_cast<uint8_t>((x + y) * 4);
            p[3] = static_cast<uint8_t>(40 + (x * 3 + y) % 180);
        }
    }
    const uint32_t blocks = (kW / 4u) * (kH / 4u);
    std::vector<uint8_t> bc4(static_cast<size_t>(blocks) * 8u);
    std::vector<uint8_t> bc5(static_cast<size_t>(blocks) * 16u);
    std::vector<uint8_t> bc3(static_cast<size_t>(blocks) * 16u);
    CHECK(gpuCompressLevel(device, ImageBlock::Bc4, rgba.data(), kW, kH, bc4.data()));
    CHECK(gpuCompressLevel(device, ImageBlock::Bc5, rgba.data(), kW, kH, bc5.data()));
    CHECK(gpuCompressLevel(device, ImageBlock::Bc3, rgba.data(), kW, kH, bc3.data()));
    CHECK(imageBlockBytes(ImageBlock::Bc4) == 8u);
    CHECK(imageBlockBytes(ImageBlock::Bc5) == 16u);
    CHECK(imageBlockBytes(ImageBlock::Bc3) == 16u);
    double sum4 = 0.0;
    double sum5 = 0.0;
    double sum3 = 0.0;
    for (uint32_t by = 0; by < kH / 4u; ++by) {
        for (uint32_t bx = 0; bx < kW / 4u; ++bx) {
            const size_t bi = static_cast<size_t>(by) * (kW / 4u) + bx;
            uint8_t rough[16];
            uint8_t r[16];
            uint8_t g[16];
            uint8_t a[16];
            uint8_t color[16 * 4];
            decodeBc4(bc4.data() + bi * 8u, rough);
            decodeBc4(bc5.data() + bi * 16u, r);
            decodeBc4(bc5.data() + bi * 16u + 8u, g);
            decodeBc1Block(bc3.data() + bi * 16u, color);
            decodeBc4(bc3.data() + bi * 16u + 8u, a);
            CHECK(bc4[bi * 8u] > bc4[bi * 8u + 1u]);
            for (uint32_t ty = 0; ty < 4; ++ty) {
                for (uint32_t tx = 0; tx < 4; ++tx) {
                    const uint8_t* src = rgba.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 4u;
                    const uint32_t i = ty * 4u + tx;
                    sum4 += std::abs(static_cast<int>(src[0]) - static_cast<int>(rough[i]));
                    sum5 += std::abs(static_cast<int>(src[0]) - static_cast<int>(r[i]));
                    sum5 += std::abs(static_cast<int>(src[1]) - static_cast<int>(g[i]));
                    const uint8_t* dec = color + i * 4u;
                    sum3 += std::abs(static_cast<int>(src[0]) - static_cast<int>(dec[0]));
                    sum3 += std::abs(static_cast<int>(src[1]) - static_cast<int>(dec[1]));
                    sum3 += std::abs(static_cast<int>(src[2]) - static_cast<int>(dec[2]));
                    sum3 += std::abs(static_cast<int>(src[3]) - static_cast<int>(a[i]));
                }
            }
        }
    }
    const double n = static_cast<double>(kW) * kH;
    spdlog::info("gpu bc4/bc5/bc3 mean abs = {:.3f} {:.3f} {:.3f}", sum4 / n, sum5 / (n * 2.0), sum3 / (n * 4.0));
    CHECK(sum4 / n < 8.0);
    CHECK(sum5 / (n * 2.0) < 8.0);
    CHECK(sum3 / (n * 4.0) < 12.0);
    deviceDestroy(device);
    windowDestroy(window);
}

// Фаза 2 (план рендера): GPU-компьют BC1 (core/image/compress.slang), проверенный golden-критерием
// лоссового кодека — не побитовое совпадение с CPU, а «декодированный результат близок к
// оригиналу». Идёт через публичный gpuImageUploadRgba (не напрямую к internal gpuCompressBc1),
// картинка без альфы форсирует BC1 и, значит, GPU-путь (CPU ISPC — только запасной).
TEST_CASE("gpuImageUploadRgba: GPU BC1 decodes close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for GPU BC1 test, skip");
        return;
    }
    Device device{};
    if (!deviceCreate(device, window)) {
        spdlog::warn("no Vulkan device for GPU BC1 test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x * 4);
            p[1] = static_cast<uint8_t>(y * 4);
            p[2] = static_cast<uint8_t>((x + y) * 2);
            p[3] = 255; // без альфы -> BC1, не BC7
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadRgba(img, device, rgba.data(), kW, kH, 1);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        const uint32_t blocksX = kW / 4u;
        const uint32_t blocksY = kH / 4u;
        const VkDeviceSize bcBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * 8u;
        GpuBuffer staging{};
        if (gpuBufferCreate(staging, device, bcBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo alloc{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = device.commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(device.device, &alloc, &cmd) == VK_SUCCESS) {
                const VkCommandBufferBeginInfo begin{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkBeginCommandBuffer(cmd, &begin);
                rhiImageBarrier(
                    cmd,
                    img.image,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                const VkBufferImageCopy region{
                    .bufferOffset = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageExtent = {kW, kH, 1},
                };
                vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
                vkEndCommandBuffer(cmd);
                const VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
                const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi};
                if (vkQueueSubmit2(device.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
                    vkQueueWaitIdle(device.graphicsQueue);
                    const auto* blocks = static_cast<const uint8_t*>(staging.mapped);
                    double sumErr = 0.0;
                    for (uint32_t by = 0; by < blocksY; ++by) {
                        for (uint32_t bx = 0; bx < blocksX; ++bx) {
                            uint8_t decoded[16 * 4];
                            decodeBc1Block(blocks + (static_cast<size_t>(by) * blocksX + bx) * 8u, decoded);
                            for (uint32_t ty = 0; ty < 4; ++ty) {
                                for (uint32_t tx = 0; tx < 4; ++tx) {
                                    const uint8_t* src = rgba.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 4u;
                                    const uint8_t* dec = decoded + (ty * 4 + tx) * 4u;
                                    sumErr += std::abs(static_cast<int>(src[0]) - dec[0]);
                                    sumErr += std::abs(static_cast<int>(src[1]) - dec[1]);
                                    sumErr += std::abs(static_cast<int>(src[2]) - dec[2]);
                                }
                            }
                        }
                    }
                    const double meanErr = sumErr / (static_cast<double>(kW) * kH * 3.0);
                    spdlog::info("gpu bc1 mean abs error per channel = {:.3f}", meanErr);
                    CHECK(meanErr < 20.0);
                }
            }
            if (cmd != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device.device, device.commandPool, 1, &cmd);
            }
            gpuBufferDestroy(staging, device);
        }
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}

// Фаза 2: BC7 mode 6 на GPU (compress.slang csBc7). Альфа форсирует BC7. Каждый блок обязан
// быть mode 6 (не ISPC), декод сравнивается с источником.
TEST_CASE("gpuImageUploadRgba: GPU BC7 mode 6 decodes close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for GPU BC7 test, skip");
        return;
    }
    Device device{};
    if (!deviceCreate(device, window)) {
        spdlog::warn("no Vulkan device for GPU BC7 test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x * 4);
            p[1] = static_cast<uint8_t>(y * 4);
            p[2] = static_cast<uint8_t>((x + y) * 2);
            p[3] = static_cast<uint8_t>(32 + (x + y) % 200);
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadRgba(img, device, rgba.data(), kW, kH, 1);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.format == VK_FORMAT_BC7_UNORM_BLOCK);
        const uint32_t blocksX = kW / 4u;
        const uint32_t blocksY = kH / 4u;
        const VkDeviceSize bcBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * 16u;
        GpuBuffer staging{};
        if (gpuBufferCreate(staging, device, bcBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo alloc{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = device.commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(device.device, &alloc, &cmd) == VK_SUCCESS) {
                const VkCommandBufferBeginInfo begin{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkBeginCommandBuffer(cmd, &begin);
                rhiImageBarrier(
                    cmd,
                    img.image,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                const VkBufferImageCopy region{
                    .bufferOffset = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageExtent = {kW, kH, 1},
                };
                vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
                vkEndCommandBuffer(cmd);
                const VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
                const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi};
                if (vkQueueSubmit2(device.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
                    vkQueueWaitIdle(device.graphicsQueue);
                    const auto* blocks = static_cast<const uint8_t*>(staging.mapped);
                    double sumErr = 0.0;
                    uint32_t mode6 = 0;
                    for (uint32_t by = 0; by < blocksY; ++by) {
                        for (uint32_t bx = 0; bx < blocksX; ++bx) {
                            uint8_t decoded[16 * 4];
                            const uint8_t* blk = blocks + (static_cast<size_t>(by) * blocksX + bx) * 16u;
                            if (!decodeBc7Mode6(blk, decoded)) {
                                continue;
                            }
                            ++mode6;
                            for (uint32_t ty = 0; ty < 4; ++ty) {
                                for (uint32_t tx = 0; tx < 4; ++tx) {
                                    const uint8_t* src = rgba.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 4u;
                                    const uint8_t* dec = decoded + (ty * 4 + tx) * 4u;
                                    for (int c = 0; c < 4; ++c) {
                                        sumErr += std::abs(static_cast<int>(src[c]) - static_cast<int>(dec[c]));
                                    }
                                }
                            }
                        }
                    }
                    const double meanErr = sumErr / (static_cast<double>(kW) * kH * 4.0);
                    spdlog::info("gpu bc7 mode6 blocks {}/{} mean abs error per channel = {:.3f}", mode6, blocksX * blocksY, meanErr);
                    CHECK(mode6 == blocksX * blocksY);
                    CHECK(meanErr < 12.0);
                }
            }
            if (cmd != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device.device, device.commandPool, 1, &cmd);
            }
            gpuBufferDestroy(staging, device);
        }
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}

// Фаза 2: BC6H mode 11 на GPU. Каждый блок обязан быть mode 11 (биты 0b00011), не ISPC.
TEST_CASE("gpuImageUploadHdr: GPU BC6H mode 11 decodes close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for GPU BC6H test, skip");
        return;
    }
    Device device{};
    if (!deviceCreate(device, window)) {
        spdlog::warn("no Vulkan device for GPU BC6H test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    std::vector<float> rgb(static_cast<size_t>(kW) * kH * 3u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            float* p = rgb.data() + (static_cast<size_t>(y) * kW + x) * 3u;
            p[0] = static_cast<float>(x) / 63.0f * 4.0f;
            p[1] = static_cast<float>(y) / 63.0f * 2.0f;
            p[2] = static_cast<float>(x + y) / 126.0f * 8.0f;
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadHdr(img, device, rgb.data(), kW, kH);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.format == VK_FORMAT_BC6H_UFLOAT_BLOCK);
        const uint32_t blocksX = kW / 4u;
        const uint32_t blocksY = kH / 4u;
        const VkDeviceSize bcBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * 16u;
        GpuBuffer staging{};
        if (gpuBufferCreate(staging, device, bcBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo alloc{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = device.commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(device.device, &alloc, &cmd) == VK_SUCCESS) {
                const VkCommandBufferBeginInfo begin{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkBeginCommandBuffer(cmd, &begin);
                rhiImageBarrier(
                    cmd,
                    img.image,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                const VkBufferImageCopy region{
                    .bufferOffset = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageExtent = {kW, kH, 1},
                };
                vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
                vkEndCommandBuffer(cmd);
                const VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
                const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi};
                if (vkQueueSubmit2(device.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
                    vkQueueWaitIdle(device.graphicsQueue);
                    const auto* blocks = static_cast<const uint8_t*>(staging.mapped);
                    double sumErr = 0.0;
                    uint32_t mode11 = 0;
                    for (uint32_t by = 0; by < blocksY; ++by) {
                        for (uint32_t bx = 0; bx < blocksX; ++bx) {
                            float decoded[16 * 3];
                            const uint8_t* blk = blocks + (static_cast<size_t>(by) * blocksX + bx) * 16u;
                            if (!decodeBc6hMode11(blk, decoded)) {
                                continue;
                            }
                            ++mode11;
                            for (uint32_t ty = 0; ty < 4; ++ty) {
                                for (uint32_t tx = 0; tx < 4; ++tx) {
                                    const float* src = rgb.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 3u;
                                    const float* dec = decoded + (ty * 4 + tx) * 3u;
                                    for (int c = 0; c < 3; ++c) {
                                        sumErr += std::abs(src[c] - dec[c]);
                                    }
                                }
                            }
                        }
                    }
                    const double meanErr = sumErr / (static_cast<double>(kW) * kH * 3.0);
                    spdlog::info("gpu bc6h mode11 blocks {}/{} mean abs error = {:.5f}", mode11, blocksX * blocksY, meanErr);
                    CHECK(mode11 == blocksX * blocksY);
                    CHECK(meanErr < 0.05);
                }
            }
            if (cmd != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device.device, device.commandPool, 1, &cmd);
            }
            gpuBufferDestroy(staging, device);
        }
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}

// Фаза 2 (план рендера): регрессия на новый путь host_image_copy в ImageFile.cpp. Картинка
// синтетическая (не файл с диска — стабильна в любом окружении), но реальная multi-mip BC7
// загрузка на реальном GPU: если submitImageHostCopy сломает байты или слой, тест ловит это
// сразу, не через визуальный скриншот руками. Пропускается без GPU/дисплея — не валит CI.
TEST_CASE("gpuImageUploadRgba round-trips through host_image_copy") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for host_image_copy test, skip");
        return;
    }
    Device device{};
    if (!deviceCreate(device, window)) {
        spdlog::warn("no Vulkan device for host_image_copy test, skip");
        windowDestroy(window);
        return;
    }
    spdlog::info("host_image_copy test: caps.hostImageCopy={}", device.caps.hostImageCopy);

    constexpr uint32_t kW = 256;
    constexpr uint32_t kH = 256;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x);
            p[1] = static_cast<uint8_t>(y);
            p[2] = static_cast<uint8_t>(x ^ y);
            p[3] = static_cast<uint8_t>((x + y) % 251); // alpha varies -> forces BC7, not BC1
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadRgba(img, device, rgba.data(), kW, kH, 1);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.extent.width == kW);
        CHECK(img.extent.height == kH);
        CHECK(img.image != VK_NULL_HANDLE);
        CHECK(img.view != VK_NULL_HANDLE);
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}

TEST_CASE("light math keeps energy penumbra and contact handoff") {
    const float smooth = hammonDiffuse(1.0f, 1.0f, 1.0f, 0.0f);
    const float rough = hammonDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(smooth > 0.2f);
    CHECK(rough > 0.0f);
    CHECK(coatLeft(1.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(coatLeft(0.0f, 1.0f) < 0.1f);
    CHECK(pcssPenumbra(0.5f, 0.5f, 1.0f) == doctest::Approx(1.0f));
    CHECK(pcssPenumbra(0.8f, 0.2f, 1.0f) > 1.0f);
    float x = 0.0f;
    float y = 0.0f;
    vogelDisk(0, 12, 0.0f, x, y);
    CHECK(x * x + y * y < 1.0f);
    float differ = 0.0f;
    const float n0 = blueNoise64(0, 0);
    for (uint32_t i = 1; i < 16; ++i) {
        differ += std::fabs(blueNoise64(i, 3) - n0);
    }
    CHECK(differ > 0.1f);
    CHECK(sscsHandoff(0.02f) == doctest::Approx(0.0f));
    CHECK(sscsHandoff(0.125f) == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(sscsHandoff(0.4f) == doctest::Approx(1.0f));
    CHECK(casDelta(1.0f, 0.5f, 0.25f) == doctest::Approx(0.125f));
    CHECK(froxelSlice(0.1f, 80.0f, 0, 16) == doctest::Approx(0.1f));
    CHECK(froxelSlice(0.1f, 80.0f, 16, 16) == doctest::Approx(80.0f));
    CHECK(ggxCone(0.0f) == 0.0f);
    CHECK(ggxCone(1.0f) > ggxCone(0.5f));
    float ox = 0.0f;
    float oy = 0.0f;
    float oz = 0.0f;
    planarReflect(0.2f, -0.4f, 0.6f, ox, oy, oz);
    CHECK(ox == -0.2f);
    CHECK(oy == -0.4f);
    CHECK(oz == -0.6f);
    CHECK(ltcForm(0.0f) == 0.0f);
    CHECK(ltcForm(1.0f) == 0.5f);
    CHECK(outdoorProbe(4u, 1u, 2u) == 1u + 3u * 4u + 2u * 16u);
    CHECK(cloudNoiseIndex(1u, 2u, 3u, 128u, 0u) == 1u + 2u * 128u + 3u * 128u * 128u);
    CHECK(cloudNoiseIndex(0u, 0u, 0u, 32u, 128u * 128u * 128u) == 128u * 128u * 128u);
    CHECK(shadowPagesCrossed(3, 4, 3, 4) == 0u);
    CHECK(shadowPagesCrossed(3, 4, 5, 4) == 3u);
    CHECK(spotPage(40.0f) == 64u);
    CHECK(spotPage(200.0f) == 512u);
    CHECK(dominantFace(0.0f, -2.0f, 0.1f) == 3u);
    CHECK(ltcCorner(0.0f, 0.0f, 1.0f, 1.0f) == 0.5f);
    CHECK(shadowPage(128, 256, 128) == 2u * (4096u / 128u) + 1u);
    CHECK(sizeof(AtmosphereParams) == 192);
    CHECK(sizeof(BloomParams) == 64);
}
