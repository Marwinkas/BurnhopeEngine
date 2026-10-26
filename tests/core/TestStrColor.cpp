#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "Str.hpp"
#include "asset/ProjectDoc.hpp"
#include "color/Color.hpp"
#include "debug/DebugStats.hpp"
#include "gfx/Material.hpp"
#include "gfx/PrimitiveGen.hpp"
#include "gpu_scene/InstanceData.hpp"
#include "gpu_scene/LightMath.hpp"
#include "gpu_scene/Mat4.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace burnhope;

TEST_CASE("path leaf and stem") {
    CHECK(std::strcmp(strPathLeaf("a/b\\c/model.v2.glb"), "model.v2.glb") == 0);
    CHECK(std::strcmp(strPathLeaf("plain"), "plain") == 0);
    CHECK(std::strcmp(strPathLeaf(nullptr), "") == 0);
    char stem[8]{};
    CHECK(strPathStem("dir/model.v2.glb", stem, sizeof(stem)) == 5);
    CHECK(std::strcmp(stem, "model") == 0);
    CHECK(strPathStem("dir/averyverylongname.obj", stem, sizeof(stem)) == 7);
    CHECK(std::strcmp(stem, "averyve") == 0);
}

TEST_CASE("line count and copy") {
    CHECK(strCountLines(nullptr) == 0);
    CHECK(strCountLines("") == 0);
    CHECK(strCountLines("one") == 1);
    CHECK(strCountLines("one\ntwo\n") == 2);
    CHECK(strCountLines("one\n\nthree") == 3);
    char line[6]{};
    CHECK(strCopyLine("one\ntwo\nthree", 1, line, sizeof(line)) == 3);
    CHECK(std::strcmp(line, "two") == 0);
    CHECK(strCopyLine("one\ntwo\nthree", 2, line, sizeof(line)) == 5);
    CHECK(strCopyLine("one\ntwo", 5, line, sizeof(line)) == 0);
    CHECK(line[0] == '\0');
    char tiny[3]{};
    CHECK(strCopyLine("abcdef", 0, tiny, sizeof(tiny)) == 2);
    CHECK(std::strcmp(tiny, "ab") == 0);
}

TEST_CASE("hex color round trip") {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    bool hasA = true;
    CHECK(colorParseHex("#FF8000", r, g, b, a, hasA));
    CHECK_FALSE(hasA);
    CHECK(r == doctest::Approx(1.0f));
    CHECK(g == doctest::Approx(128.0f / 255.0f));
    CHECK(b == doctest::Approx(0.0f));
    CHECK(a == doctest::Approx(1.0f));
    CHECK(colorParseHex("0080ff40", r, g, b, a, hasA));
    CHECK(hasA);
    CHECK(a == doctest::Approx(64.0f / 255.0f));
    CHECK_FALSE(colorParseHex("#12345", r, g, b, a, hasA));
    CHECK_FALSE(colorParseHex("#GG0000", r, g, b, a, hasA));
    CHECK_FALSE(colorParseHex(nullptr, r, g, b, a, hasA));
    char out[16]{};
    CHECK(colorFormatHex(out, sizeof(out), 1.0f, 0.5f, 0.0f, 0.25f, true) == 9);
    CHECK(std::strcmp(out, "#FF800040") == 0);
    CHECK(colorFormatHex(out, sizeof(out), 1.0f, 0.5f, 0.0f, 0.25f, false) == 7);
    CHECK(std::strcmp(out, "#FF8000") == 0);
    char small[5]{};
    CHECK(colorFormatHex(small, sizeof(small), 1.0f, 1.0f, 1.0f, 1.0f, false) == 4);
    CHECK(std::strcmp(small, "#FFF") == 0);
}

namespace {

bool takeOne(void* user, const char*, const char*) {
    ++*static_cast<int*>(user);
    return true;
}

} // namespace

TEST_CASE("projectListMaterials finds .bhmat files and skips build folders") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "bh_proj_list";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "sub", ec);
    fs::create_directories(root / "build", ec);
    for (const fs::path& file : {root / "a.bhmat", root / "sub" / "b.bhmat", root / "build" / "c.bhmat", root / "d.txt"}) {
        std::FILE* f = std::fopen(file.string().c_str(), "wb");
        CHECK(f != nullptr);
        if (f != nullptr) {
            std::fclose(f);
        }
    }
    int count = 0;
    burnhope::projectListMaterials(root.string().c_str(), takeOne, &count);
    CHECK(count == 2);
    fs::remove_all(root, ec);
}

TEST_CASE("materialCommitMaps parks maps next to the material") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "bh_commit_maps";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "src", ec);
    fs::create_directories(root / "mat", ec);
    const fs::path map = root / "src" / "tex.png";
    std::FILE* f = std::fopen(map.string().c_str(), "wb");
    CHECK(f != nullptr);
    if (f != nullptr) {
        std::fclose(f);
    }
    burnhope::BhmatDoc doc{};
    std::snprintf(doc.albedoPath, sizeof(doc.albedoPath), "%s", map.string().c_str());
    const std::string matPath = (root / "mat" / "m.bhmat").string();
    CHECK_FALSE(burnhope::materialCommitMaps(doc, matPath.c_str(), 0));
    CHECK(fs::exists(root / "mat" / "tex.png"));
    CHECK(fs::path(doc.albedoPath).parent_path() == root / "mat");
    fs::remove_all(root, ec);
}

TEST_CASE("case-insensitive substring match, empty needle matches, null never does") {
    CHECK(burnhope::strContainsCi("Rock_NORMAL.png", "normal"));
    CHECK(burnhope::strContainsCi(std::string_view("Bark"), "ARK"));
    CHECK_FALSE(burnhope::strContainsCi("rough", "metal"));
    CHECK_FALSE(burnhope::strContainsCi("ab", "abc"));
    CHECK(burnhope::strContainsCi("abc", ""));
    CHECK_FALSE(burnhope::strContainsCi(static_cast<const char*>(nullptr), "a"));
}

TEST_CASE("luma, kelvin tint and half floats match their reference values") {
    CHECK(burnhope::colorLuma(1.0f, 1.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(burnhope::colorLuma(0.0f, 1.0f, 0.0f) == doctest::Approx(0.7152f));
    float r = 0.0f, g = 0.0f, b = 0.0f;
    burnhope::colorFromKelvin(6600.0f, r, g, b);
    CHECK(r == doctest::Approx(1.0f));
    CHECK(g > 0.95f);
    CHECK(b > 0.95f);
    burnhope::colorFromKelvin(1000.0f, r, g, b);
    float rLow = 0.0f, gLow = 0.0f, bLow = 0.0f;
    burnhope::colorFromKelvin(1500.0f, rLow, gLow, bLow);
    CHECK(r == rLow);
    CHECK(g == gLow);
    CHECK(b == bLow);
    CHECK(burnhope::floatToHalf(1.0f) == 0x3c00u);
    CHECK(burnhope::floatToHalf(-2.0f) == 0xc000u);
    CHECK(burnhope::floatToHalf(0.0f) == 0u);
    CHECK(burnhope::floatToHalf(1.0e10f) == 0x7c00u);
}

TEST_CASE("strAppendLine adds a line, truncates, and skips when full") {
    char text[16]{};
    strCopyCap(text, sizeof(text), "one");
    CHECK(strAppendLine(text, sizeof(text), "two") == 7);
    CHECK(std::strcmp(text, "one\ntwo") == 0);
    CHECK(strAppendLine(text, sizeof(text), "0123456789") == 15);
    CHECK(std::strcmp(text, "one\ntwo\n0123456") == 0);
    CHECK(strAppendLine(text, sizeof(text), "x") == 15);
    CHECK(std::strcmp(text, "one\ntwo\n0123456") == 0);
}

TEST_CASE("frame stats report fps, worst frame and the slowest percent") {
    float ms[100];
    for (float& v : ms) {
        v = 10.0f;
    }
    ms[7] = 40.0f;
    char out[96]{};
    debugFormatFrameStats(ms, 100, 90.0f, 3, 9, out, sizeof(out));
    CHECK(std::strcmp(out, "fps 90   low 25   1% 25   mesh 3/9") == 0);
    debugFormatFrameStats(nullptr, 0, 60.0f, 0, 0, out, sizeof(out));
    CHECK(std::strcmp(out, "fps 60   low 60   1% 60   mesh 0/0") == 0);
}

TEST_CASE("material channel slots resolve to the GPU record") {
    MaterialGpuData mat{};
    uint32_t none = 7u;
    CHECK(materialTexSlot(mat, 0, &none) == &mat.albedoTexIndex);
    CHECK(none == 0xffffffffu);
    CHECK(materialTexSlot(mat, 2, &none) == &mat.ormTexIndex);
    CHECK(none == 0u);
    CHECK(materialTexSlot(mat, 6) == &mat.aoTex);
    CHECK(materialTexSlot(mat, 8) == &mat.tail[1]);
    CHECK(materialTexSlot(mat, 9) == nullptr);
}

TEST_CASE("primitive boxes, instance points and axis bases") {
    GpuVertex vert[24]{};
    uint32_t idx[36]{};
    genBoxMesh(vert, idx, 1.0f, 2.0f, 3.0f);
    CHECK(idx[35] == 23u);
    float maxY = 0.0f;
    for (const GpuVertex& v : vert) {
        maxY = v.py > maxY ? v.py : maxY;
    }
    CHECK(maxY == doctest::Approx(2.0f));
    GpuInstance inst{};
    instanceSet(inst, 1.0f, 2.0f, 3.0f, 1.0f, 0, 0);
    float x = 0.0f, y = 0.0f, z = 0.0f;
    instanceTransformPoint(inst, 1.0f, 1.0f, 1.0f, x, y, z);
    CHECK(x == doctest::Approx(2.0f));
    CHECK(y == doctest::Approx(3.0f));
    CHECK(z == doctest::Approx(4.0f));
    const float axis[3] = {0.0f, 0.0f, 1.0f};
    float u[3]{}, v[3]{};
    CHECK(mat4BasisFromAxis(axis, u, v));
    CHECK(u[0] * axis[0] + u[1] * axis[1] + u[2] * axis[2] == doctest::Approx(0.0f));
    CHECK(u[0] * v[0] + u[1] * v[1] + u[2] * v[2] == doctest::Approx(0.0f));
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    CHECK_FALSE(mat4BasisFromAxis(zero, u, v));
}

TEST_CASE("vector helpers, euler basis roundtrip and reverse-Z box") {
    const float a[3] = {1.0f, 2.0f, 2.0f};
    const float b[3] = {4.0f, 6.0f, 2.0f};
    float out[3]{};
    vec3Add(a, b, out);
    CHECK(out[0] == doctest::Approx(5.0f));
    vec3Sub(b, a, out);
    CHECK(out[2] == doctest::Approx(0.0f));
    CHECK(vec3Dot(a, b) == doctest::Approx(20.0f));
    CHECK(vec3Len(a) == doctest::Approx(3.0f));
    CHECK(vec3Dist(a, b) == doctest::Approx(5.0f));
    float n[3] = {a[0], a[1], a[2]};
    CHECK(vec3Normalize(n) == doctest::Approx(3.0f));
    CHECK(vec3Len(n) == doctest::Approx(1.0f));
    float zero[3]{};
    CHECK(vec3Normalize(zero) == doctest::Approx(0.0f));
    const float ex[3] = {1.0f, 0.0f, 0.0f};
    const float ey[3] = {0.0f, 1.0f, 0.0f};
    vec3Cross(ex, ey, out);
    CHECK(out[2] == doctest::Approx(1.0f));

    const float euler[3] = {20.0f, 35.0f, -15.0f};
    float x[3], y[3], z[3], back[3];
    mat3FromEuler(euler, x, y, z);
    eulerFromMat3(x, y, z, back);
    CHECK(back[0] == doctest::Approx(euler[0]).epsilon(0.001));
    CHECK(back[1] == doctest::Approx(euler[1]).epsilon(0.001));
    CHECK(back[2] == doctest::Approx(euler[2]).epsilon(0.001));

    const Mat4 box = mat4OrthoBoxReverseZ(10.0f, 10.0f, 1.0f, 101.0f);
    const Mat4 ref = mat4OrthoReverseZ(10.0f, 10.0f, 1.0f, 101.0f);
    CHECK(box.m[10] * -1.0f + box.m[14] == doctest::Approx(1.0f)); // near plane, view z = -1 -> depth 1
    CHECK(box.m[10] * -101.0f + box.m[14] == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(box.m[0] == doctest::Approx(ref.m[0]));
}

TEST_CASE("morton codes and triangle transform") {
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 64u; ++x) {
            uint32_t rx = 0, ry = 0;
            morton2DDecode(morton2DEncode(x, y), rx, ry);
            CHECK(rx == x);
            CHECK(ry == y);
        }
    }
    CHECK(morton2DEncode(1u, 0u) == 1u);
    CHECK(morton2DEncode(0u, 1u) == 2u);
    CHECK(morton2DEncode(7u, 3u) == 31u);

    GpuVertex a{}, b{}, c{};
    b.px = 2.0f;
    c.py = 2.0f;
    GpuInstance inst{};
    instanceSet(inst, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0);
    float p0[3], p1[3], p2[3], normal[3], area2 = 0.0f;
    instanceTransformTriangle(inst, a, b, c, p0, p1, p2, normal, area2);
    CHECK(p0[2] == doctest::Approx(1.0f));
    CHECK(p1[0] == doctest::Approx(2.0f));
    CHECK(normal[2] == doctest::Approx(4.0f));
    CHECK(area2 == doctest::Approx(16.0f));
}
