#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "Str.hpp"
#include "color/Color.hpp"

#include <cstring>

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
