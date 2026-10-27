#include "app/MaterialInternal.hpp"

#include "app/BrowserThumbs.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "gui/GuiHost.hpp"
#include "render/SceneFrame.hpp"
#include "platform/PlatformUtil.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr float kPi = 3.14159265f;

// One slot card: the atlas cell of its map and the path field of the document it edits.
struct Card {
    uint8_t cell;
    char (BhmatDoc::*path)[260];
};

constexpr Card kCards[8] = {
    {0, &BhmatDoc::albedoPath},     {3, &BhmatDoc::normalPath},
    {1, &BhmatDoc::metalPath},       {4, &BhmatDoc::heightPath},
    {8, &BhmatDoc::alphaPath},      {2, &BhmatDoc::roughPath},
    {5, &BhmatDoc::aoPath},            {7, &BhmatDoc::emissivePath},
};

// Scalar sliders (kScalarKey order) -> document field.
struct Scalar {
    float BhmatDoc::*value;
};

constexpr Scalar kScalars[7] = {
    {&BhmatDoc::normalScale}, {&BhmatDoc::metallic},     {&BhmatDoc::heightScale},
    {&BhmatDoc::baseA},           {&BhmatDoc::roughness},    {&BhmatDoc::aoStrength},
    {&BhmatDoc::emissive},
};

// Field indices: 0..6 scalars, 7..14 card paths, 15 albedo hex, 16 emission hex, 17..20 tiling x/y offset x/y.
constexpr uint32_t kFieldPath = 7;
constexpr uint32_t kFieldHexBase = 15;
constexpr uint32_t kFieldHexEmission = 16;
constexpr uint32_t kFieldVec = 17;

// JSON "bind" keys of assets/gui/material.json, fixed at compile time (no key building per frame).
constexpr const char* kScalarKey[7] = {"mat.normal.scale", "mat.metal.value", "mat.height.value", "mat.alpha.value",
                                       "mat.rough.value",  "mat.ao.value",    "mat.emission.value"};
constexpr const char* kPathKey[8] = {"mat.albedo.path", "mat.normal.path", "mat.metal.path", "mat.height.path",
                                     "mat.alpha.path",  "mat.rough.path",  "mat.ao.path",    "mat.emission.path"};
constexpr const char* kThumbKey[8] = {"mat.albedo.thumb", "mat.normal.thumb", "mat.metal.thumb", "mat.height.thumb",
                                      "mat.alpha.thumb",  "mat.rough.thumb",  "mat.ao.thumb",    "mat.emission.thumb"};
constexpr const char* kVecKey[4] = {"mat.tiling.x", "mat.tiling.y", "mat.offset.x", "mat.offset.y"};

float* vecSlot(BhmatDoc& d, uint32_t i) {
    return (i & 2u) != 0 ? &d.uvOffset[i & 1u] : &d.uvTiling[i & 1u];
}

float* hexSlot(BhmatDoc& d, uint32_t which) {
    return which == kFieldHexBase ? d.base : d.emissiveColor;
}

void formatHex(char* out, size_t cap, const float rgb[3]) {
    unsigned v[3];
    for (int i = 0; i < 3; ++i) {
        v[i] = static_cast<unsigned>(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
    }
    std::snprintf(out, cap, "#%02X%02X%02X", v[0], v[1], v[2]);
}

bool parseHex(const char* text, float rgb[3]) {
    if (text[0] == '#') {
        ++text;
    }
    if (std::strlen(text) != 6) {
        return false;
    }
    float out[3];
    for (int i = 0; i < 3; ++i) {
        unsigned v = 0;
        const auto r = std::from_chars(text + i * 2, text + i * 2 + 2, v, 16);
        if (r.ec != std::errc{} || r.ptr != text + i * 2 + 2) {
            return false;
        }
        out[i] = static_cast<float>(v) / 255.0f;
    }
    std::copy_n(out, 3, rgb);
    return true;
}

float* scalarSlot(BhmatDoc& d, uint32_t i) {
    return &(d.*kScalars[i].value);
}

void touch(MaterialState& m) {
    m.previewDirty = true;
    m.sceneDirty = true;
}

void onScalar(void* user, float value) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = *f->state;
    float* slot = scalarSlot(m.doc, f->index);
    if (*slot != value) {
        *slot = value;
        touch(m);
    }
}

void onVec(void* user, float value) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = *f->state;
    float* slot = vecSlot(m.doc, f->index - kFieldVec);
    if (*slot != value) {
        *slot = value;
        touch(m);
    }
}

void onPath(void* user, const char* text) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = *f->state;
    char* dst = m.doc.*kCards[f->index - kFieldPath].path;
    if (std::strncmp(dst, text, 260) != 0) {
        (void)strCopyCap(dst, 260, text);
        m.atlasDirty = true;
        m.sceneDirty = true;
    }
}

void onHex(void* user, const char* text) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = *f->state;
    float rgb[3];
    if (parseHex(text, rgb)) {
        std::copy_n(rgb, 3, hexSlot(m.doc, f->index));
        m.atlasDirty = true;
        m.sceneDirty = true;
    }
}

void onOrbit(void* user, float x, float y) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    m.yaw = (x - 0.5f) * 2.0f * kPi;
    m.pitch = (y - 0.5f) * kPi * 0.9f;
    touch(m);
}

} // namespace

void materialTouch(MaterialState& m) {
    touch(m);
}

void materialPushFlip(MaterialState& m) {
    guiHostSetText(m.gui, "mat.normal.flip", m.doc.flipNormalY != 0 ? "Flip Y: on" : "Flip Y: off");
    guiHostSetActive(m.gui, "mat.normal.flip", m.doc.flipNormalY != 0);
}

void materialPushVec(MaterialState& m) {
    for (uint32_t i = 0; i < 4; ++i) {
        guiHostSetValue(m.gui, kVecKey[i], *vecSlot(m.doc, i));
    }
}

// Mirrors the whole document into the GUI.
void materialPushDoc(MaterialState& m) {
    for (uint32_t i = 0; i < 7; ++i) {
        guiHostSetValue(m.gui, kScalarKey[i], *scalarSlot(m.doc, i));
    }
    for (uint32_t i = 0; i < 8; ++i) {
        guiHostSetText(m.gui, kPathKey[i], m.doc.*kCards[i].path);
        guiHostSetPhoto(m.gui, kThumbKey[i], 9, kCards[i].cell, 4, 3);
    }
    char hex[16];
    formatHex(hex, sizeof(hex), m.doc.base);
    guiHostSetText(m.gui, "mat.albedo.hex", hex);
    formatHex(hex, sizeof(hex), m.doc.emissiveColor);
    guiHostSetText(m.gui, "mat.emission.hex", hex);
    materialPushVec(m);
    materialPushFlip(m);
    const char* name = std::strrchr(m.path, '/');
    guiHostSetText(m.gui, "mat.title", name != nullptr ? name + 1 : m.path);
    static constexpr const char* kShapeKey[3] = {"mat.shape.sphere", "mat.shape.cube", "mat.shape.plane"};
    for (uint32_t s = 0; s < 3; ++s) {
        guiHostSetActive(m.gui, kShapeKey[s], m.shape == s);
    }
}

bool materialBindFields(MaterialState& m) {
    bool ok = true;
    for (uint32_t i = 0; i < 7; ++i) {
        m.field[i] = {&m, i};
        ok = guiHostBindDrag(m.gui, kScalarKey[i], onScalar, &m.field[i]) && ok;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        m.field[kFieldPath + i] = {&m, kFieldPath + i};
        ok = guiHostBindEdit(m.gui, kPathKey[i], onPath, &m.field[kFieldPath + i]) && ok;
    }
    m.field[kFieldHexBase + 8] = {&m, kFieldHexBase};
    m.field[kFieldHexEmission + 8] = {&m, kFieldHexEmission};
    ok = guiHostBindEdit(m.gui, "mat.albedo.hex", onHex, &m.field[kFieldHexBase + 8]) && ok;
    ok = guiHostBindEdit(m.gui, "mat.emission.hex", onHex, &m.field[kFieldHexEmission + 8]) && ok;
    for (uint32_t i = 0; i < 4; ++i) {
        m.field[kFieldVec + i] = {&m, kFieldVec + i};
        ok = guiHostBindDrag(m.gui, kVecKey[i], onVec, &m.field[kFieldVec + i]) && ok;
    }
    return guiHostBindDrag2(m.gui, "mat.preview", onOrbit, &m) && ok;
}


} // namespace burnhope
