#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "anim/Animation.hpp"
#include "time/Time.hpp"
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

#include "image/ImageDecode.hpp"

#include <cstdio>
#include <filesystem>

TEST_CASE("imageLoadFile rasterizes an svg with a Cyrillic path") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bh_svg_тест";
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "иконка.svg").string();
    const char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\">"
                       "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" fill=\"#ff0000\"/></svg>";
    FILE* f = std::fopen(file.c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr) {
        return;
    }
    std::fwrite(svg, 1, sizeof(svg) - 1, f);
    std::fclose(f);
    ImageCpu img{};
    const bool loaded = imageLoadFile(img, file.c_str());
    CHECK(loaded);
    if (!loaded) {
        return;
    }
    CHECK(img.width == 64);
    CHECK(img.height == 64);
    const uint8_t* mid = img.rgba + (32u * img.width + 32u) * 4u;
    CHECK(mid[0] == 255);
    CHECK(mid[1] == 0);
    CHECK(mid[3] == 255);
    imageCpuFree(img);
    std::filesystem::remove_all(dir);
}
