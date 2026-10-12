#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "anim/Animation.hpp"
#include "time/Time.hpp"
#include "input/Input.hpp"
#include "color/Color.hpp"
#include "doc/Document.hpp"
#include "wasm/WasmHost.hpp"
#include "memory/FrameArena.hpp"

#include <cstring>

using namespace burnhope;

int32_t wasmTick(void*, const int32_t*, uint32_t) {
    return 1;
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
    uint8_t* mem = wasmMemory(host, memBytes);
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
