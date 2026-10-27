#include "app/MaterialInternal.hpp"

#include "gui/GuiHost.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr float kPi = 3.14159265f;

// Scalar sliders (kScalarKey order): document field and the value the R button restores.
struct Scalar {
    float BhmatDoc::*value;
    float reset;
};

constexpr Scalar kScalars[kMatScalarCount] = {
    {&BhmatDoc::normalScale, 1.0f}, {&BhmatDoc::metallic, 0.0f}, {&BhmatDoc::heightScale, 0.0f}, {&BhmatDoc::baseA, 1.0f},
    {&BhmatDoc::roughness, 1.0f},   {&BhmatDoc::aoStrength, 1.0f}, {&BhmatDoc::emissive, 0.0f},
};

// Field indices: 0..6 scalars, 7..10 ORMH twins (index = the scalar they mirror), 11..14 tiling x/y offset x/y.
constexpr uint32_t kFieldOrm = 7;
constexpr uint32_t kFieldVec = 11;
// Orbit drag: a full pad width turns the model once around, the pitch stops short of the poles.
constexpr float kOrbitYawPerPad = 2.0f * kPi;
constexpr float kOrbitPitchPerPad = kPi * 0.9f;
constexpr float kOrbitPitchMax = kPi * 0.45f;

// JSON "bind" keys of assets/gui/material.json, fixed at compile time (no key building per frame).
constexpr const char* kScalarKey[kMatScalarCount] = {"mat.normal.scale", "mat.metal.value", "mat.height.value", "mat.alpha.value",
                                                     "mat.rough.value",  "mat.ao.value",    "mat.emission.value"};
constexpr const char* kOrmKey[4] = {"mat.orm.rough", "mat.orm.metal", "mat.orm.ao", "mat.orm.height"};
constexpr uint32_t kOrmScalar[4] = {4, 1, 5, 2};
constexpr const char* kSwatchKey[2] = {"mat.albedo.swatch", "mat.emission.swatch"};
constexpr const char* kVecKey[4] = {"mat.tiling.x", "mat.tiling.y", "mat.offset.x", "mat.offset.y"};

float* vecSlot(BhmatDoc& d, uint32_t i) {
    return (i & 2u) != 0 ? &d.uvOffset[i & 1u] : &d.uvTiling[i & 1u];
}

float* colorSlot(BhmatDoc& d, uint32_t which) {
    return which == 0 ? d.base : d.emissiveColor;
}

void onScalar(void* user, float value) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = *f->state;
    float& slot = m.doc.*kScalars[f->index].value;
    if (slot != value) {
        slot = value;
        materialPushScalar(m, f->index);
        materialTouch(m);
    }
}

void onVec(void* user, float value) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = *f->state;
    float* slot = vecSlot(m.doc, f->index - kFieldVec);
    if (*slot != value) {
        *slot = value;
        materialTouch(m);
    }
}

// The pad reports the pointer as a fraction of its box. The first report of a gesture is only the grab point; every later one
// turns the model by the travel since the previous report, so a press never jumps the model to the pressed angle.
void onOrbit(void* user, float x, float y) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    if (m.orbitHeld) {
        m.yaw += (x - m.orbitX) * kOrbitYawPerPad;
        m.pitch = std::clamp(m.pitch + (y - m.orbitY) * kOrbitPitchPerPad, -kOrbitPitchMax, kOrbitPitchMax);
        m.previewDirty = true;
    }
    m.orbitHeld = true;
    m.orbitX = x;
    m.orbitY = y;
}

} // namespace

void materialTouch(MaterialState& m) {
    m.previewDirty = true;
    m.sceneDirty = true;
}

void materialTouchAtlas(MaterialState& m) {
    m.atlasDirty = true;
    m.sceneDirty = true;
}

void materialPushFlip(MaterialState& m) {
    guiHostSetActive(m.gui, "mat.normal.flip", m.doc.flipNormalY != 0);
}

void materialPushVec(MaterialState& m) {
    for (uint32_t i = 0; i < 4; ++i) {
        guiHostSetValue(m.gui, kVecKey[i], *vecSlot(m.doc, i));
    }
}

void materialPushScalar(MaterialState& m, uint32_t index) {
    const float value = m.doc.*kScalars[index].value;
    guiHostSetValue(m.gui, kScalarKey[index], value);
    for (uint32_t j = 0; j < 4; ++j) {
        if (kOrmScalar[j] == index) {
            guiHostSetValue(m.gui, kOrmKey[j], value);
        }
    }
}

void materialPushColors(MaterialState& m) {
    for (uint32_t i = 0; i < 2; ++i) {
        const float* rgb = colorSlot(m.doc, i);
        guiHostSetColor(m.gui, kSwatchKey[i], rgb[0], rgb[1], rgb[2]);
    }
}

void materialResetScalar(MaterialState& m, uint32_t index) {
    if (index < kMatScalarCount) {
        m.doc.*kScalars[index].value = kScalars[index].reset;
        materialPushScalar(m, index);
        materialTouch(m);
    }
}

void materialSetColor(MaterialState& m, uint32_t which, const float rgb[3]) {
    std::copy_n(rgb, 3, colorSlot(m.doc, which));
    materialPushColors(m);
    materialTouchAtlas(m);
}

// Mirrors the whole document into the GUI.
void materialPushDoc(MaterialState& m) {
    for (uint32_t i = 0; i < kMatScalarCount; ++i) {
        materialPushScalar(m, i);
    }
    materialPushSlots(m);
    materialPushColors(m);
    materialPushVec(m);
    materialPushFlip(m);
    materialPushModes(m);
    static constexpr const char* kShapeKey[3] = {"mat.shape.sphere", "mat.shape.cube", "mat.shape.plane"};
    for (uint32_t s = 0; s < 3; ++s) {
        guiHostSetActive(m.gui, kShapeKey[s], m.shape == s);
    }
}

bool materialBindFields(MaterialState& m) {
    bool ok = true;
    for (uint32_t i = 0; i < kMatScalarCount; ++i) {
        m.field[i] = {&m, i};
        ok = guiHostBindDrag(m.gui, kScalarKey[i], onScalar, &m.field[i]) && ok;
    }
    for (uint32_t j = 0; j < 4; ++j) {
        m.field[kFieldOrm + j] = {&m, kOrmScalar[j]};
        ok = guiHostBindDrag(m.gui, kOrmKey[j], onScalar, &m.field[kFieldOrm + j]) && ok;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        m.field[kFieldVec + i] = {&m, kFieldVec + i};
        ok = guiHostBindDrag(m.gui, kVecKey[i], onVec, &m.field[kFieldVec + i]) && ok;
    }
    return guiHostBindDrag2(m.gui, "mat.preview", onOrbit, &m) && ok;
}

} // namespace burnhope
