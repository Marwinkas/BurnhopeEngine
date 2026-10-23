#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Canvas.hpp"
#include "ui/Domain.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/UiPhoto.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/TreeView.hpp"
#include "Str.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void onActor(void* user, uint16_t) {
    auto* hit = static_cast<ObjectHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->scene == nullptr) {
        return;
    }
    sceneFrameActorSelect(hit->scene, hit->index);
    if (hit->hud != nullptr) {
        hit->hud->matOpen = 0;
    }
    if (hit->canvas->state != nullptr) {
        uiPropShowMask(*hit->canvas->state, sceneFrameActorSections(hit->scene, hit->index));
    }
}

bool addActorButton(HudContext& hud, uint16_t objects, uint8_t index, const char* name, uint8_t kind) {
    if (hud.tool.canvas == nullptr || objects == kUiNone) {
        return false;
    }
    Canvas& canvas = *hud.tool.canvas;
    if (canvas.state == nullptr) {
        return false;
    }
    uint16_t rowBtn = uiButton(*canvas.state, objects, { .label = name, .fillWidth = true });
    if (rowBtn == kUiNone) {
        return false;
    }
    uiSetHeight(*canvas.state, rowBtn, 28.0f);
    hud.objects.push_back({});
    ObjectHit& hit = hud.objects.back();
    hit.canvas = &canvas;
    hit.scene = hud.tool.scene;
    hit.hud = &hud;
    hit.index = index;
    (void)kind;
    uiBind(*canvas.state, rowBtn, onActor, &hit);
    return true;
}

void paintTools(HudContext& hud) {
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud.tool.canvas->state;
    const uint8_t modes[5] = {3, 0, 1, 2, 4};
    for (int i = 0; i < 5; ++i) {
        const bool on = modes[i] == hud.toolMode;
        uiButtonTintIcon(s, hud.toolIcon[i], on);
        if (hud.toolIcon[i] < s.count) {
            if (auto* paint = s.ent[hud.toolIcon[i]].try_get_mut<UiPaint>()) {
                paint->bright = on ? 1.0f : 0.7f;
            }
        }
        if (hud.toolBtn[i] < s.count) {
            s.ent[hud.toolBtn[i]].add<UiIconChrome>();
            if (auto* paint = s.ent[hud.toolBtn[i]].try_get_mut<UiPaint>()) {
                paintGhost(*paint);
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        uiButtonTintIcon(s, hud.snapIcon[i], hud.snapOn[i] != 0);
        if (hud.snapBtn[i] < s.count) {
            s.ent[hud.snapBtn[i]].add<UiIconChrome>();
            if (auto* paint = s.ent[hud.snapBtn[i]].try_get_mut<UiPaint>()) {
                paintGhost(*paint);
                paint->a = hud.snapOn[i] != 0 ? 0.18f : 0.0f;
            }
        }
    }
}

void onGizmoMode(void* user, uint16_t) {
    auto* hit = static_cast<GizmoHit*>(user);
    if (hit == nullptr || hit->tool == nullptr || hit->tool->scene == nullptr) {
        return;
    }
    sceneFrameActorGizmo(hit->tool->scene, hit->mode);
    if (hit->hud != nullptr) {
        hit->hud->toolMode = hit->mode;
        paintTools(*hit->hud);
    }
}

void onSpace(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->space = index;
    if (hud->tool.scene != nullptr) {
        sceneFrameGizmoSpace(hud->tool.scene, index);
    }
}

void onSnapToggle(void* user, uint16_t) {
    auto* hit = static_cast<GizmoHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->mode > 2) {
        return;
    }
    HudContext& hud = *hit->hud;
    hud.snapOn[hit->mode] = hud.snapOn[hit->mode] == 0 ? 1 : 0;
    paintTools(hud);
    if (hud.tool.scene == nullptr) {
        return;
    }
    sceneFrameSnap(hud.tool.scene, hit->mode, hud.snapOn[hit->mode], hud.snapSent[hit->mode]);
}

void onSnapPick(void* user, uint16_t) {
    auto* hit = static_cast<SnapHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->channel > 2) {
        return;
    }
    hit->hud->snapPick[hit->channel] = hit->index;
    hit->hud->snapSent[hit->channel] = hit->step;
    if (hit->hud->tool.canvas != nullptr) {
        setSlide(*hit->hud->tool.canvas, hit->hud->snapNum[hit->channel], hit->step);
    }
    if (hit->hud->tool.scene != nullptr) {
        sceneFrameSnap(hit->hud->tool.scene, hit->channel, hit->hud->snapOn[hit->channel], hit->step);
    }
}

void onPivotSpace(void* user, uint8_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->pivotSpace = index == 0 ? 0 : 1;
    if (hud->tool.scene != nullptr) {
        sceneFrameGizmoPivot(hud->tool.scene, hud->pivotSpace);
    }
}

void spawnAndSelectActor(HudContext& hud, uint8_t index) {
    if (index == kSceneActorNone || hud.tool.scene == nullptr) {
        return;
    }
    char name[24];
    uint8_t kind = 0;
    sceneFrameActorName(hud.tool.scene, index, name, sizeof(name), &kind);
    if (addActorButton(hud, hud.tool.objects, index, name, kind)) {
        onActor(&hud.objects.back(), 0);
    }
}

void onSpawnKind(void* user, uint16_t) {
    auto* hit = static_cast<SpawnHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->hud->tool.scene == nullptr) {
        return;
    }
    spawnAndSelectActor(*hit->hud, sceneFrameSpawn(hit->hud->tool.scene, hit->kind));
}

void onPlay(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud->tool.canvas->state;
    if (sceneFrameSim(hud->tool.scene) == 0) {
        sceneFramePlay(hud->tool.scene);
        uiShow(s, hud->pauseBtn, true);
        if (auto* paint = s.ent[hud->playGlyph].try_get_mut<UiPaint>()) {
            paint->icon = 19;
        }
    } else {
        sceneFrameStop(hud->tool.scene);
        uiShow(s, hud->pauseBtn, false);
        if (auto* paint = s.ent[hud->playGlyph].try_get_mut<UiPaint>()) {
            paint->icon = 17;
        }
    }
    uiNotifyVisualDirty(s);
    uiNotifyLayoutDirty(s);
}

void onPause(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr) {
        return;
    }
    const uint8_t sim = sceneFrameSim(hud->tool.scene);
    sceneFramePause(hud->tool.scene, sim == 1 ? 1 : 0);
}

void onGear(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->statsWant = hud->statsWant == 2 ? 0 : 1;
}

void onCameraButton(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->camWant = hud->camWant == 2 ? 0 : 1;
}

void onMode(void* user);

void onViewButton(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->viewWant = hud->viewWant == 2 ? 0 : 1;
}

void onViewRadio(void* user, uint16_t id) {
    auto* hit = static_cast<ModeHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->canvas->state == nullptr) {
        return;
    }
    uiCheckPress(*hit->canvas->state, id);
    onMode(hit);
    if (hit->hud != nullptr) {
        hit->hud->viewWant = 0;
    }
}

void onSpawnCube(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr) {
        return;
    }
    spawnAndSelectActor(*hud, sceneFrameSpawn(hud->tool.scene, 0));
}

void onDuplicate(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr || hud->tool.canvas == nullptr) {
        return;
    }
    spawnAndSelectActor(*hud, sceneFrameActorDuplicate(hud->tool.scene));
}

} // namespace burnhope
