#include "app/MaterialController.hpp"

#include "app/Engine.hpp"
#include "app/BrowserThumbs.hpp"
#include "app/GuiActions.hpp"
#include "platform/PlatformUtil.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

namespace burnhope {
namespace {

constexpr const char* kPanel = "main::matPanel";
constexpr float kPi = 3.14159265f;

// One slot card: its node name, the atlas cell of its map and the path field of the document it edits.
struct Card {
    const char* column;
    const char* name;
    uint8_t cell;
    char (BhmatDoc::*path)[260];
};

constexpr Card kCards[8] = {
    {"left", "albedo", 0, &BhmatDoc::albedoPath},     {"left", "normal", 3, &BhmatDoc::normalPath},
    {"left", "metal", 1, &BhmatDoc::metalPath},       {"left", "height", 4, &BhmatDoc::heightPath},
    {"right", "alpha", 8, &BhmatDoc::alphaPath},      {"right", "rough", 2, &BhmatDoc::roughPath},
    {"right", "ao", 5, &BhmatDoc::aoPath},            {"right", "emission", 7, &BhmatDoc::emissivePath},
};

// Scalar sliders: node "<card>::row::ctl::<leaf>" -> document field.
struct Scalar {
    uint32_t card;
    const char* leaf;
    float BhmatDoc::*value;
};

constexpr Scalar kScalars[7] = {
    {1, "scale", &BhmatDoc::normalScale}, {2, "v", &BhmatDoc::metallic},     {3, "v", &BhmatDoc::heightScale},
    {4, "v", &BhmatDoc::baseA},           {5, "v", &BhmatDoc::roughness},    {6, "v", &BhmatDoc::aoStrength},
    {7, "v", &BhmatDoc::emissive},
};

// Field indices: 0..6 scalars, 7..14 card paths, 15 albedo hex, 16 emission hex, 17..20 tiling x/y offset x/y.
constexpr uint32_t kFieldPath = 7;
constexpr uint32_t kFieldHexBase = 15;
constexpr uint32_t kFieldHexEmission = 16;
constexpr uint32_t kFieldVec = 17;

void cardPath(char* out, size_t cap, const Card& c, const char* leaf) {
    std::snprintf(out, cap, "%s::cols::%s::%s::%s", kPanel, c.column, c.name, leaf);
}

void vecPath(char* out, size_t cap, uint32_t i) {
    std::snprintf(out, cap, "%s::%s::%c", kPanel, (i & 2u) != 0 ? "offset" : "tiling", (i & 1u) != 0 ? 'y' : 'x');
}

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
}

void onScalar(void* user, float value) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = f->engine->material;
    float* slot = scalarSlot(m.doc, f->index);
    if (*slot != value) {
        *slot = value;
        touch(m);
    }
}

void onVec(void* user, float value) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = f->engine->material;
    float* slot = vecSlot(m.doc, f->index - kFieldVec);
    if (*slot != value) {
        *slot = value;
        touch(m);
    }
}

void onPath(void* user, const char* text) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = f->engine->material;
    char* dst = m.doc.*kCards[f->index - kFieldPath].path;
    if (std::strncmp(dst, text, 260) != 0) {
        (void)strCopyCap(dst, 260, text);
        m.atlasDirty = true;
    }
}

void onHex(void* user, const char* text) {
    auto* f = static_cast<MaterialField*>(user);
    MaterialState& m = f->engine->material;
    float rgb[3];
    if (parseHex(text, rgb)) {
        std::copy_n(rgb, 3, hexSlot(m.doc, f->index));
        m.atlasDirty = true;
    }
}

void onOrbit(void* user, float x, float y) {
    MaterialState& m = static_cast<Engine*>(user)->material;
    m.yaw = (x - 0.5f) * 2.0f * kPi;
    m.pitch = (y - 0.5f) * kPi * 0.9f;
    touch(m);
}

void pushFlip(Engine& e) {
    char node[160];
    std::snprintf(node, sizeof(node), "%s::cols::left::normal::row::ctl::flip", kPanel);
    guiHostSetText(e.gui, node, e.material.doc.flipNormalY != 0 ? "Flip Y: on" : "Flip Y: off");
    guiHostSetActive(e.gui, node, e.material.doc.flipNormalY != 0);
}

// Mirrors the whole document into the GUI.
void pushDoc(Engine& e) {
    MaterialState& m = e.material;
    char node[160];
    for (uint32_t i = 0; i < 7; ++i) {
        cardPath(node, sizeof(node), kCards[kScalars[i].card], i == 0 ? "row::ctl::scale" : "row::ctl::v");
        guiHostSetValue(e.gui, node, *scalarSlot(m.doc, i));
    }
    for (uint32_t i = 0; i < 8; ++i) {
        cardPath(node, sizeof(node), kCards[i], "path");
        guiHostSetText(e.gui, node, m.doc.*kCards[i].path);
        cardPath(node, sizeof(node), kCards[i], "row::thumb");
        guiHostSetPhoto(e.gui, node, 9, kCards[i].cell, 4, 3);
    }
    char hex[16];
    formatHex(hex, sizeof(hex), m.doc.base);
    cardPath(node, sizeof(node), kCards[0], "row::ctl::hex");
    guiHostSetText(e.gui, node, hex);
    formatHex(hex, sizeof(hex), m.doc.emissiveColor);
    cardPath(node, sizeof(node), kCards[7], "row::ctl::hex");
    guiHostSetText(e.gui, node, hex);
    for (uint32_t i = 0; i < 4; ++i) {
        vecPath(node, sizeof(node), i);
        guiHostSetValue(e.gui, node, *vecSlot(m.doc, i));
    }
    pushFlip(e);
    const char* name = std::strrchr(m.path, '/');
    std::snprintf(node, sizeof(node), "%s::head::title", kPanel);
    guiHostSetText(e.gui, node, name != nullptr ? name + 1 : m.path);
    for (uint32_t s = 0; s < 3; ++s) {
        static constexpr const char* kShapeNode[3] = {"sphere", "cube", "plane"};
        std::snprintf(node, sizeof(node), "%s::shapes::%s", kPanel, kShapeNode[s]);
        guiHostSetActive(e.gui, node, m.shape == s);
    }
}

void onClick(void* user, uint32_t action) {
    Engine& e = *static_cast<Engine*>(user);
    MaterialState& m = e.material;
    switch (action) {
    case kActMatClose:
        m.open = false;
        guiHostSetShown(e.gui, kPanel, false);
        return;
    case kActMatFlipY:
        m.doc.flipNormalY = m.doc.flipNormalY != 0 ? 0 : 1;
        pushFlip(e);
        touch(m);
        return;
    case kActMatResetTiling:
    case kActMatResetOffset: {
        const bool tiling = action == kActMatResetTiling;
        m.doc.uvTiling[0] = tiling ? 1.0f : m.doc.uvTiling[0];
        m.doc.uvTiling[1] = tiling ? 1.0f : m.doc.uvTiling[1];
        if (!tiling) {
            m.doc.uvOffset[0] = m.doc.uvOffset[1] = 0.0f;
        }
        char node[160];
        for (uint32_t i = 0; i < 4; ++i) {
            vecPath(node, sizeof(node), i);
            guiHostSetValue(e.gui, node, *vecSlot(m.doc, i));
        }
        touch(m);
        return;
    }
    case kActMatSave:
        if (m.open && materialSaveBhmat(m.path, m.doc)) {
            spdlog::info("material saved: {}", m.path);
            thumbsInvalidate(e, m.path);
        } else {
            spdlog::error("material save failed: {}", m.path);
        }
        return;
    default:
        break;
    }
    if (action >= kActMatShape && action < kActMatShape + 3) {
        m.shape = static_cast<uint8_t>(action - kActMatShape);
        pushDoc(e);
        touch(m);
    }
}

} // namespace

bool materialBind(Engine& e) {
    MaterialState& m = e.material;
    m.atlas.reset(new (std::nothrow) uint8_t[static_cast<size_t>(kMatAtlasW) * kMatAtlasH * 4u]());
    m.preview.reset(new (std::nothrow) uint8_t[static_cast<size_t>(kMatSphereSide) * kMatSphereSide * 4u]());
    char screen[512]{};
    if (m.atlas == nullptr || m.preview == nullptr || !platformResolveAsset("assets/gui/material.json", screen, sizeof(screen)) ||
        !guiHostMount(e.gui, screen, "main")) {
        spdlog::error("material editor panel failed to mount");
        return false;
    }
    bool ok = true;
    char node[160];
    for (uint32_t i = 0; i < 7; ++i) {
        m.field[i] = {&e, i};
        cardPath(node, sizeof(node), kCards[kScalars[i].card], i == 0 ? "row::ctl::scale" : "row::ctl::v");
        ok = guiHostBindDrag(e.gui, node, onScalar, &m.field[i]) && ok;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        m.field[kFieldPath + i] = {&e, kFieldPath + i};
        cardPath(node, sizeof(node), kCards[i], "path");
        ok = guiHostBindEdit(e.gui, node, onPath, &m.field[kFieldPath + i]) && ok;
    }
    m.field[kFieldHexBase + 8] = {&e, kFieldHexBase};
    m.field[kFieldHexEmission + 8] = {&e, kFieldHexEmission};
    cardPath(node, sizeof(node), kCards[0], "row::ctl::hex");
    ok = guiHostBindEdit(e.gui, node, onHex, &m.field[kFieldHexBase + 8]) && ok;
    cardPath(node, sizeof(node), kCards[7], "row::ctl::hex");
    ok = guiHostBindEdit(e.gui, node, onHex, &m.field[kFieldHexEmission + 8]) && ok;
    for (uint32_t i = 0; i < 4; ++i) {
        m.field[kFieldVec + i] = {&e, kFieldVec + i};
        vecPath(node, sizeof(node), i);
        ok = guiHostBindDrag(e.gui, node, onVec, &m.field[kFieldVec + i]) && ok;
    }
    std::snprintf(node, sizeof(node), "%s::pvRow::preview", kPanel);
    ok = guiHostBindDrag2(e.gui, node, onOrbit, &e) && ok;
    for (const uint32_t action : std::initializer_list<uint32_t>{kActMatClose, kActMatFlipY, kActMatResetTiling, kActMatResetOffset, kActMatSave, kActMatShape,
                            kActMatShape + 1u, kActMatShape + 2u}) {
        ok = guiHostBindClick(e.gui, action, onClick, &e) && ok;
    }
    return ok;
}

bool materialOpen(Engine& e, const char* path) {
    MaterialState& m = e.material;
    BhmatDoc doc{};
    if (path == nullptr || !materialLoadBhmat(path, doc)) {
        spdlog::warn("material: cannot read {}", path != nullptr ? path : "(null)");
        return false;
    }
    m.doc = doc;
    (void)strCopyCap(m.path, sizeof(m.path), path);
    m.open = true;
    m.atlasDirty = true;
    m.previewDirty = true;
    guiHostSetShown(e.gui, kPanel, true);
    pushDoc(e);
    return true;
}

void materialSync(Engine& e) {
    MaterialState& m = e.material;
    Canvas* canvas = hostCanvas(e.host, e.slot);
    if (!m.open || canvas == nullptr || (!m.atlasDirty && !m.previewDirty)) {
        return;
    }
    if (m.atlasDirty) {
        materialBakeAtlas(m.doc, m.path, m.atlas.get());
        if (canvasUploadPhoto(*canvas, 9, m.atlas.get(), kMatAtlasW, kMatAtlasH)) {
            m.atlasDirty = false;
            m.previewDirty = true;
        }
    }
    if (m.previewDirty) {
        materialBakePreview(m.doc, m.atlas.get(), static_cast<MatShape>(m.shape), m.yaw, m.pitch, 1.0f, m.preview.get());
        if (canvasUploadPhoto(*canvas, 8, m.preview.get(), kMatSphereSide, kMatSphereSide)) {
            m.previewDirty = false;
        }
    }
}

} // namespace burnhope
