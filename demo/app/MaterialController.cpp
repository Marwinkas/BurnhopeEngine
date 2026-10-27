#include "app/MaterialController.hpp"

#include "gui/GuiHost.hpp"
#include "app/MaterialInternal.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "platform/Input.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <initializer_list>

namespace burnhope {
namespace {

// 0 Lit, 1 Unlit, 2 ClearCoat.
uint32_t typeOf(const BhmatDoc& d) {
    return d.unlit != 0 ? 1u : (d.clearcoat > 0.001f ? 2u : 0u);
}

void setType(MaterialState& m, uint32_t type) {
    m.doc.unlit = type == 1 ? 1 : 0;
    m.doc.clearcoat = type == 2 ? std::max(m.doc.clearcoat, 1.0f) : 0.0f;
}

void setFlag(MaterialState& m, uint8_t& flag) {
    flag = flag != 0 ? 0 : 1;
    materialPushModes(m);
    materialTouch(m);
}

void onClick(void* user, uint32_t action) {
    MaterialState& m = *static_cast<MaterialState*>(user);
    if (action >= kActMatResetScalar && action < kActMatResetScalar + kMatScalarCount) {
        materialResetScalar(m, action - kActMatResetScalar);
        return;
    }
    if (action >= kActMatType && action < kActMatType + 3) {
        setType(m, action - kActMatType);
        materialPushModes(m);
        materialTouch(m);
        return;
    }
    if (action >= kActMatMaps && action < kActMatMaps + 2) {
        m.doc.mapPack = static_cast<uint8_t>(action - kActMatMaps);
        materialPushModes(m);
        materialPushSlots(m);
        materialTouchAtlas(m);
        return;
    }
    if (action >= kActMatShape && action < kActMatShape + 3) {
        m.shape = static_cast<uint8_t>(action - kActMatShape);
        materialPushDoc(m);
        materialTouch(m);
        return;
    }
    switch (action) {
    case kActMatTypeOpen:
        guiHostOpenAt(m.gui, "matTypeMenu", "mat.type.button");
        return;
    case kActMatMapsOpen:
        guiHostOpenAt(m.gui, "matMapsMenu", "mat.maps.button");
        return;
    case kActMatDouble:
        setFlag(m, m.doc.doubleSided);
        return;
    case kActMatRepeat:
        setFlag(m, m.doc.repeat);
        return;
    case kActMatTriplanar:
        setFlag(m, m.doc.worldAligned);
        return;
    case kActMatFlipY:
        m.doc.flipNormalY = m.doc.flipNormalY != 0 ? 0 : 1;
        materialPushFlip(m);
        materialTouch(m);
        return;
    case kActMatResetTiling:
    case kActMatResetOffset: {
        const bool tiling = action == kActMatResetTiling;
        float* v = tiling ? m.doc.uvTiling : m.doc.uvOffset;
        v[0] = v[1] = tiling ? 1.0f : 0.0f;
        materialPushVec(m);
        materialTouch(m);
        return;
    }
    case kActMatResetAlbedo:
    case kActMatResetEmission: {
        static constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};
        materialSetColor(m, action == kActMatResetAlbedo ? 0u : 1u, kWhite);
        return;
    }
    case kActMatSave:
        if (m.open && materialSaveBhmat(m.path, m.doc)) {
            spdlog::info("material saved: {}", m.path);
            m.saved(m.app, m.path);
        } else {
            spdlog::error("material save failed: {}", m.path);
        }
        return;
    default:
        return;
    }
}

// The document as the scene shows it: only the maps of the chosen workflow reach the actor.
BhmatDoc sceneDoc(const BhmatDoc& doc) {
    BhmatDoc out = doc;
    if (out.mapPack == 0) {
        out.ormPath[0] = '\0';
    } else {
        out.roughPath[0] = out.metalPath[0] = out.aoPath[0] = out.heightPath[0] = '\0';
    }
    return out;
}

} // namespace

void materialPushModes(MaterialState& m) {
    static constexpr const char* kTypeName[3] = {"Lit", "Unlit", "ClearCoat"};
    guiHostSetText(m.gui, "mat.type.button", kTypeName[typeOf(m.doc)]);
    guiHostSetText(m.gui, "mat.maps.button", m.doc.mapPack != 0 ? "ORMH" : "Classic");
    guiHostSetActive(m.gui, "mat.flag.double", m.doc.doubleSided != 0);
    guiHostSetActive(m.gui, "mat.flag.repeat", m.doc.repeat != 0);
    guiHostSetActive(m.gui, "mat.flag.tri", m.doc.worldAligned != 0);
}

bool materialBind(MaterialState& m, GuiHost& gui, SceneFrame* scene, void* app, MaterialSavedFn saved, MaterialThumbFn thumb) {
    m.gui = &gui;
    m.scene = scene;
    m.app = app;
    m.saved = saved;
    m.thumb = thumb;
    m.atlas.assign(static_cast<size_t>(kMatAtlasW) * kMatAtlasH * 4u, 0);
    m.preview.assign(static_cast<size_t>(kMatSphereSide) * kMatSphereSide * 4u, 0);
    char screen[512]{};
    if (!platformResolveAsset("assets/gui/material.json", screen, sizeof(screen)) ||
        !guiHostSpawnSnippet(m.gui, "dock.right", screen)) {
        spdlog::error("material editor panel failed to mount");
        return false;
    }
    bool ok = materialBindFields(m);
    ok = materialBindPickers(m) && ok;
    for (const uint32_t action : std::initializer_list<uint32_t>{kActMatFlipY, kActMatResetTiling, kActMatResetOffset, kActMatSave,
                                                                 kActMatTypeOpen, kActMatMapsOpen, kActMatDouble, kActMatRepeat, kActMatTriplanar,
                                                                 kActMatResetAlbedo, kActMatResetEmission}) {
        ok = guiHostBindClick(m.gui, action, onClick, &m) && ok;
    }
    for (uint32_t i = 0; i < 3; ++i) {
        ok = guiHostBindClick(m.gui, kActMatShape + i, onClick, &m) && ok;
        ok = guiHostBindClick(m.gui, kActMatType + i, onClick, &m) && ok;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        ok = guiHostBindClick(m.gui, kActMatMaps + i, onClick, &m) && ok;
    }
    for (uint32_t i = 0; i < kMatScalarCount; ++i) {
        ok = guiHostBindClick(m.gui, kActMatResetScalar + i, onClick, &m) && ok;
    }
    return ok;
}

bool materialOpen(MaterialState& m, const char* path) {
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
    m.sceneDirty = false;
    const uint8_t picked = sceneFrameActorSelected(m.scene);
    m.target = picked < sceneFrameActorCount(m.scene) ? picked : kSceneActorNone;
    (void)guiHostDockOpen(m.gui, "mat.panel");
    materialPushDoc(m);
    return true;
}

bool materialAssign(MaterialState& m, const char* path, uint8_t actor) {
    BhmatDoc doc{};
    if (path == nullptr || actor >= sceneFrameActorCount(m.scene) || !materialLoadBhmat(path, doc)) {
        return false;
    }
    if (!sceneFrameApplyDoc(m.scene, actor, sceneDoc(doc))) {
        spdlog::warn("material: cannot apply {} to actor {}", path, actor);
        return false;
    }
    if (!materialOpen(m, path)) {
        return false;
    }
    m.target = actor;
    return true;
}

void materialSync(MaterialState& m, Canvas* canvas) {
    float px = 0.0f;
    float py = 0.0f;
    uint32_t down = 0;
    guiHostPointer(m.gui, px, py, down);
    if ((down & kPointerLeft) == 0) {
        m.orbitHeld = false;
    }
    materialPickSync(m);
    if (m.open && m.sceneDirty && m.target != kSceneActorNone) {
        if (sceneFrameApplyDoc(m.scene, m.target, sceneDoc(m.doc))) {
            m.sceneDirty = false;
        }
    }
    if (!m.open || canvas == nullptr || (!m.atlasDirty && !m.previewDirty)) {
        return;
    }
    if (m.atlasDirty) {
        materialBakeAtlas(m.doc, m.path, m.atlas.data());
        if (canvasUploadPhoto(*canvas, 9, m.atlas.data(), kMatAtlasW, kMatAtlasH)) {
            m.atlasDirty = false;
            m.previewDirty = true;
        }
    }
    if (m.previewDirty) {
        materialBakePreview(m.doc, m.atlas.data(), static_cast<MatShape>(m.shape), m.yaw, m.pitch, 1.0f, m.preview.data());
        if (canvasUploadPhoto(*canvas, 8, m.preview.data(), kMatSphereSide, kMatSphereSide)) {
            m.previewDirty = false;
        }
    }
}

} // namespace burnhope
