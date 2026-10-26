#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/SceneBlockFill.hpp"

#include "render/SceneFrame.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <cstdio>

namespace burnhope {

void hudShowBodies(HudContext& hud, UiState& s) {
    propApplyShow(s, hud.bind);
}

void hudBuildInspector(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t stats) {
    UiPaint hidden{};
    hidden.a = 0.0f;
    uint16_t panel = stats;
    hud->bind.attach(hud->block.scalars, 256, hud->block.flags, 64);
    hud->ids.statsLbl = uiLabel(*canvas.state, panel, "fps");
    hud->ids.memLbl = uiLabel(*canvas.state, panel, "mem");
    hud->ids.loadLbl = uiLabel(*canvas.state, panel, "gpu");
    auto tick = [&](const char* label, uint16_t index, bool on) {
        hud->bind.check(*canvas.state, panel, label, index, on);
    };
    const uint8_t catGeneral = static_cast<uint8_t>(UiPropCat::General);
    const uint8_t catActor = static_cast<uint8_t>(UiPropCat::Actor);
    panel = uiPropSection(*canvas.state, details, "Transform", catGeneral, kSecTransform);
    constexpr float kOpen = 3.4028234e+38f;
    hud->bind.vec3(*canvas.state, panel, "Location", kScPos0, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f);
    hud->bind.vec3(*canvas.state, panel, "Rotation", kScEuler0, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.5f);
    hud->bind.vec3(*canvas.state, panel, "Scale", kScScale0, 1.0f, 1.0f, 1.0f, -kOpen, kOpen, 0.05f, true);
    hud->bind.vec3(*canvas.state, panel, "Pivot", kScPivot0, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f);
    tick("Static", kFlActorStatic, false);
    panel = uiPropSection(*canvas.state, details, "Material", catActor, kSecMaterial);
    hudBuildActorMaterial(canvas, hud, panel);
    hudBuildPost(canvas, hud, details, hidden);
    uint16_t matFile = kUiNone;
    const PropSlot matSlot[] = {{"matFile", &matFile}};
    (void)propImportAsset(*canvas.state, details, "assets/ui/inspector.json", "material", hud->bind, matSlot, 1, nullptr, 0, hidden, onImportFlag, hud);
    hudBuildMaterialPage(canvas, hud, matFile);
    panel = stats;
    hud->ids.statList = uiVirtual(*canvas.state, panel, 16.0f);
    if (canvas.state != nullptr) {
        hudShowBodies(*hud, *canvas.state);
        uint8_t mask = kSecTransform;
        if (hud->tool.scene != nullptr) {
            const uint8_t sel = sceneFrameActorSelected(hud->tool.scene);
            if (sel != kSceneActorNone) {
                mask = sceneFrameActorSections(hud->tool.scene, sel);
            }
        }
        uiPropShowMask(*canvas.state, mask);
        uiEnsureColorPopup(*canvas.state);
        if (hud->book != nullptr) {
            uiSetColorBook(*canvas.state, hud->book);
        }
    }
    sceneBlockApply(*hud);
}
} // namespace burnhope
