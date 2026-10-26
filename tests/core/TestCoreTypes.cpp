#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "anim/Animation.hpp"
#include "time/Time.hpp"
#include "color/Color.hpp"
#include "doc/Document.hpp"
#include "wasm/WasmHost.hpp"
#include "memory/FrameArena.hpp"

#include <cstring>
#include <vector>

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
    CHECK(mid[0] == 255); // svg paints rasterize as white masks; the UI shader tints them
    CHECK(mid[1] == 255);
    CHECK(mid[2] == 255);
    CHECK(mid[3] == 255);
    imageCpuFree(img);
    std::filesystem::remove_all(dir);
}

#include "io/File.hpp"
#include "ui/Icon.hpp"

TEST_CASE("uiIconFromName resolves names and rejects unknown ones") {
    CHECK(uiIconFromName("plus") == UiIcon::Add);
    CHECK(uiIconFromName("gear") == UiIcon::Cogwheel);
    CHECK(uiIconFromName("chevron_left") == UiIcon::ChevronLeft);
    CHECK(uiIconFromName("nope") == UiIcon::None);
    CHECK(std::string_view(uiIconAssetPath(UiIcon::Add)) == "assets/icons/plus.svg");
    CHECK(uiIconAssetPath(UiIcon::None) == nullptr);
}

TEST_CASE("fileTrimPath strips CRLF, spaces and quotes from the tail") {
    char out[64];
    CHECK(fileTrimPath("a/b.jpg\r\n", out, sizeof(out)) == 7);
    CHECK(std::strcmp(out, "a/b.jpg") == 0);
    CHECK(fileTrimPath("\"x.png\" ", out, sizeof(out)) == 6);
    CHECK(fileTrimPath("long/path", out, 4) == 0);
}

#include "asset/ModelMesh.hpp"

namespace {

void putBits(uint8_t* block, uint32_t at, uint32_t n, uint32_t value) {
    for (uint32_t i = 0; i < n; ++i) {
        if (((value >> i) & 1u) != 0) {
            block[(at + i) / 8] = static_cast<uint8_t>(block[(at + i) / 8] | (1u << ((at + i) % 8)));
        }
    }
}

bool decodeOne(ImageBlock format, const uint8_t* block, uint32_t blockBytes, uint8_t out[4]) {
    uint8_t file[128 + 16]{};
    std::memcpy(file, "DDS ", 4);
    const uint32_t size = 4;
    std::memcpy(file + 12, &size, 4);
    std::memcpy(file + 16, &size, 4);
    const uint32_t mips = 1;
    std::memcpy(file + 28, &mips, 4);
    const char* four = format == ImageBlock::Bc1 ? "DXT1" : format == ImageBlock::Bc3 ? "DXT5" : format == ImageBlock::Bc5 ? "ATI2" : "BC7 ";
    std::memcpy(file + 84, four, 4);
    std::memcpy(file + 128, block, blockBytes);
    DdsView view{};
    uint8_t rgba[4 * 4 * 4]{};
    if (!imageParseDds(file, 128 + blockBytes, view) || !imageDecodeDdsLevel(view, 0, rgba)) {
        return false;
    }
    std::memcpy(out, rgba, 4);
    return true;
}

} // namespace

TEST_CASE("dds DX10 extension header selects the block format from dxgiFormat") {
    const uint8_t bc1[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    const uint32_t dxgi[] = {71, 72, 98, 99, 77, 83, 84, 28};
    const bool good[] = {true, true, true, true, true, true, true, false};
    for (size_t i = 0; i < std::size(dxgi); ++i) {
        uint8_t file[148 + 16]{};
        std::memcpy(file, "DDS ", 4);
        const uint32_t side = 4;
        const uint32_t mips = 1;
        std::memcpy(file + 12, &side, 4);
        std::memcpy(file + 16, &side, 4);
        std::memcpy(file + 28, &mips, 4);
        std::memcpy(file + 84, "DX10", 4);
        std::memcpy(file + 128, &dxgi[i], 4);
        std::memcpy(file + 148, bc1, 8);
        DdsView view{};
        CHECK(imageParseDds(file, sizeof(file), view) == good[i]);
        if (good[i]) {
            CHECK(view.levels[0].offset == 148);
            CHECK(view.block == (dxgi[i] <= 72 ? ImageBlock::Bc1 : dxgi[i] <= 78 ? ImageBlock::Bc3 : dxgi[i] <= 84 ? ImageBlock::Bc5 : ImageBlock::Bc7));
        }
    }
    uint8_t shortFile[140]{};
    std::memcpy(shortFile, "DDS ", 4);
    std::memcpy(shortFile + 84, "DX10", 4);
    DdsView view{};
    CHECK(!imageParseDds(shortFile, sizeof(shortFile), view));
}

TEST_CASE("dds block decoders expand BC1 BC3 BC5 and BC7 modes 5 and 6") {
    uint8_t px[4]{};
    const uint8_t bc1[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    CHECK(decodeOne(ImageBlock::Bc1, bc1, 8, px));
    CHECK(px[0] == 255);
    CHECK(px[1] == 0);
    CHECK(px[2] == 0);
    CHECK(px[3] == 255);
    uint8_t bc3[16]{};
    bc3[0] = 128;
    std::memcpy(bc3 + 8, bc1, 8);
    CHECK(decodeOne(ImageBlock::Bc3, bc3, 16, px));
    CHECK(px[0] == 255);
    CHECK(px[3] == 128);
    uint8_t bc5[16]{};
    bc5[0] = 255;
    CHECK(decodeOne(ImageBlock::Bc5, bc5, 16, px));
    CHECK(px[0] == 255);
    CHECK(px[1] == 0);
    uint8_t mode6[16]{};
    putBits(mode6, 6, 1, 1);
    putBits(mode6, 7, 7, 127);
    putBits(mode6, 21, 7, 127);
    putBits(mode6, 35, 7, 127);
    putBits(mode6, 49, 7, 127);
    putBits(mode6, 63, 1, 1);
    CHECK(decodeOne(ImageBlock::Bc7, mode6, 16, px));
    CHECK(px[0] == 255);
    CHECK(px[1] == 255);
    CHECK(px[2] == 255);
    CHECK(px[3] == 255);
    uint8_t mode5[16]{};
    putBits(mode5, 5, 1, 1);
    putBits(mode5, 6, 2, 1); // rotation 1 swaps red and alpha
    putBits(mode5, 8, 7, 127);
    CHECK(decodeOne(ImageBlock::Bc7, mode5, 16, px));
    CHECK(px[0] == 0);
    CHECK(px[1] == 0);
    CHECK(px[2] == 0);
    CHECK(px[3] == 255);
}

TEST_CASE("imageLoadFile decodes a block compressed dds") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bh_dds_test";
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "red.dds").string();
    uint8_t bytes[128 + 8]{};
    std::memcpy(bytes, "DDS ", 4);
    const uint32_t side = 4;
    std::memcpy(bytes + 12, &side, 4);
    std::memcpy(bytes + 16, &side, 4);
    std::memcpy(bytes + 84, "DXT1", 4);
    bytes[128] = 0x00;
    bytes[129] = 0xF8;
    FILE* f = std::fopen(file.c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr) {
        return;
    }
    std::fwrite(bytes, 1, sizeof(bytes), f);
    std::fclose(f);
    ImageCpu img{};
    CHECK(imageLoadFile(img, file.c_str()));
    CHECK(img.width == 4);
    CHECK(img.height == 4);
    if (img.rgba != nullptr) {
        CHECK(img.rgba[0] == 255);
        CHECK(img.rgba[1] == 0);
        CHECK(img.rgba[3] == 255);
    }
    imageCpuFree(img);
    std::filesystem::remove_all(dir);
}

TEST_CASE("modelLoadMesh reads an obj triangle") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bh_model_test";
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "tri.obj").string();
    const char obj[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    FILE* f = std::fopen(file.c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr) {
        return;
    }
    std::fwrite(obj, 1, sizeof(obj) - 1, f);
    std::fclose(f);
    ModelMesh mesh;
    CHECK(modelLoadMesh(file.c_str(), mesh));
    CHECK(mesh.idx.size() == 3);
    CHECK(mesh.xyz.size() == 9);
    CHECK(!modelLoadMesh((dir / "missing.obj").string().c_str(), mesh));
    std::filesystem::remove_all(dir);
}

#include "app/ModelThumb.hpp"
#include "files/FileThumb.hpp"

TEST_CASE("modelThumbBake draws a lit silhouette into its atlas cell only") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bh_model_thumb";
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "cube.obj").string();
    const char obj[] = "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
                       "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 4 7 3\nf 4 8 7\nf 1 5 8\nf 1 8 4\nf 2 3 7\nf 2 7 6\n";
    FILE* f = std::fopen(file.c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr) {
        return;
    }
    std::fwrite(obj, 1, sizeof(obj) - 1, f);
    std::fclose(f);
    constexpr uint32_t side = kThumbCols * kThumbPx;
    std::vector<uint8_t> atlas(static_cast<size_t>(side) * side * 4u, 0);
    CHECK(modelThumbBake(atlas.data(), 9, file.c_str()));
    const uint32_t ox = (9 % kThumbCols) * kThumbPx;
    const uint32_t oy = (9 / kThumbCols) * kThumbPx;
    const uint8_t* mid = atlas.data() + (static_cast<size_t>(oy + 32) * side + ox + 32) * 4u;
    CHECK(mid[3] == 255);
    CHECK(mid[2] > 60);
    const uint8_t* corner = atlas.data() + (static_cast<size_t>(oy) * side + ox) * 4u;
    CHECK(corner[3] == 0);
    CHECK(atlas[3] == 0); // cell 0 untouched
    CHECK(!modelThumbBake(atlas.data(), 9, (dir / "none.obj").string().c_str()));
    std::filesystem::remove_all(dir);
}
