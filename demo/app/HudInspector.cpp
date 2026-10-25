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

void onHudSwatch(void* user, uint16_t id) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud->tool.canvas->state;
    UiColorPickerComponent* color = uiColor(s);
    ColorBook* book = color != nullptr ? color->book : nullptr;
    if (book != nullptr) {
        for (const UiBind& row : hud->bind.rows) {
            if (row.kind != kBindSwatch || row.id != id || static_cast<uint32_t>(row.index) + 2u >= 256u) {
                continue;
            }
            book->r = hud->block.scalars[row.index];
            book->g = hud->block.scalars[row.index + 1u];
            book->b = hud->block.scalars[row.index + 2u];
            book->a = 1.0f;
            book->gen += 1;
            break;
        }
    }
    colorOnOpen(&s, id);
}

uint16_t bindSwatch(HudContext& hud, UiState& s, uint16_t panel, const char* label, uint16_t index, float r0, float g0, float b0) {
    const uint16_t id = hud.bind.swatch(s, panel, label, index, r0, g0, b0);
    uiBind(s, id, onHudSwatch, &hud);
    return id;
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
    auto vec = [&](uint16_t host, const char* label, uint16_t index, float x, float y, float z, float minV, float maxV, float step, int slot) {
        const UiPropVec3 row = uiPropVec3(*canvas.state, host, label, x, y, z, minV, maxV, step);
        hud->bind.watch(row.x, index, x);
        hud->bind.watch(row.y, static_cast<uint16_t>(index + 1u), y);
        hud->bind.watch(row.z, static_cast<uint16_t>(index + 2u), z);
        if (slot >= 0 && slot < 4) {
            hud->xformLock[slot] = row.lock;
        }
    };
    const uint8_t catGeneral = static_cast<uint8_t>(UiPropCat::General);
    const uint8_t catActor = static_cast<uint8_t>(UiPropCat::Actor);
    panel = uiPropSection(*canvas.state, details, "Transform", catGeneral, kSecTransform);
    constexpr float kOpen = 3.4028234e+38f;
    vec(panel, "Location", kScPos0, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f, 0);
    vec(panel, "Rotation", kScEuler0, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.5f, 1);
    vec(panel, "Scale", kScScale0, 1.0f, 1.0f, 1.0f, -kOpen, kOpen, 0.05f, 2);
    vec(panel, "Pivot", kScPivot0, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f, 3);
    tick("Static", kFlActorStatic, false);
    panel = uiPropSection(*canvas.state, details, "Material", catActor, kSecMaterial);
    hudBuildActorMaterial(canvas, hud, panel);
    hudBuildPost(canvas, hud, details, hidden);
    uint16_t matFile = kUiNone;
    const PropSlot matSlot[] = {{"matFile", &matFile}};
    (void)hudImportLayout(*canvas.state, details, "assets/ui/inspector.json", "material", *hud, matSlot, 1, nullptr, 0, hidden);
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
