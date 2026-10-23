#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/Domain.hpp"
#include "ui/State.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Image.hpp"

#include <cstring>
#include <vector>

using namespace burnhope;

TEST_CASE("curve is a gpu ribbon") {
    UiState state;
    const uint16_t id = spawn(state, kUiNone, px(180, 80), paintRgb(0.1f, 0.1f, 0.1f));
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
TEST_CASE("photo blur packs one lod into the photo primitive") {
    UiState state;
    const uint16_t id = uiIcon(state, kUiNone, 0, 64.0f, 64.0f);
    uiLayout(state, 64.0f, 64.0f);
    uiBlur(state, id, 1.5f);
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
    const uint16_t root = spawn(state, kUiNone, px(200, 80), paintRgb(0.1f, 0.1f, 0.1f));
    uiLayout(state, 200.0f, 80.0f);
    uiTransform(state, root, 30.0f, 1.2f, 1.0f);
    uiFilter(state, root, 1.3f, 0.7f);
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
