#include "app/MaterialController.hpp"

#include "app/BrowserThumbs.hpp"
#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "app/MaterialInternal.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <initializer_list>

namespace burnhope {
namespace {

void onClick(void* user, uint32_t action) {
    Engine& e = *static_cast<Engine*>(user);
    MaterialState& m = e.material;
    switch (action) {
    case kActMatClose:
        m.open = false;
        m.target = kSceneActorNone;
        guiHostSetShown(e.gui, "mat.panel", false);
        return;
    case kActMatFlipY:
        m.doc.flipNormalY = m.doc.flipNormalY != 0 ? 0 : 1;
        materialPushFlip(e);
        materialTouch(m);
        return;
    case kActMatResetTiling:
    case kActMatResetOffset: {
        const bool tiling = action == kActMatResetTiling;
        m.doc.uvTiling[0] = tiling ? 1.0f : m.doc.uvTiling[0];
        m.doc.uvTiling[1] = tiling ? 1.0f : m.doc.uvTiling[1];
        if (!tiling) {
            m.doc.uvOffset[0] = m.doc.uvOffset[1] = 0.0f;
        }
        materialPushVec(e);
        materialTouch(m);
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
        materialPushDoc(e);
        materialTouch(m);
    }
}

} // namespace

bool materialBind(Engine& e) {
    MaterialState& m = e.material;
    m.atlas.assign(static_cast<size_t>(kMatAtlasW) * kMatAtlasH * 4u, 0);
    m.preview.assign(static_cast<size_t>(kMatSphereSide) * kMatSphereSide * 4u, 0);
    char screen[512]{};
    if (!platformResolveAsset("assets/gui/material.json", screen, sizeof(screen)) ||
        !guiHostMount(e.gui, screen, "main")) {
        spdlog::error("material editor panel failed to mount");
        return false;
    }
    bool ok = materialBindFields(e);
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
    m.sceneDirty = false;
    const uint8_t picked = sceneFrameActorSelected(e.scene);
    m.target = picked < sceneFrameActorCount(e.scene) ? picked : kSceneActorNone;
    guiHostSetShown(e.gui, "mat.panel", true);
    materialPushDoc(e);
    return true;
}

bool materialAssign(Engine& e, const char* path, uint8_t actor) {
    BhmatDoc doc{};
    if (path == nullptr || actor >= sceneFrameActorCount(e.scene) || !materialLoadBhmat(path, doc)) {
        return false;
    }
    if (!sceneFrameApplyDoc(e.scene, actor, doc)) {
        spdlog::warn("material: cannot apply {} to actor {}", path, actor);
        return false;
    }
    if (!materialOpen(e, path)) {
        return false;
    }
    e.material.target = actor;
    return true;
}

void materialSync(Engine& e) {
    MaterialState& m = e.material;
    Canvas* canvas = hostCanvas(e.host, e.slot);
    if (m.open && m.sceneDirty && m.target != kSceneActorNone) {
        if (sceneFrameApplyDoc(e.scene, m.target, m.doc)) {
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
