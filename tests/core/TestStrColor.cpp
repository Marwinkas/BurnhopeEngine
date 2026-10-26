#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "Str.hpp"
#include "asset/ProjectDoc.hpp"
#include "color/Color.hpp"

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
