#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/rhi/Canvas.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/rhi/UiPhoto.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/TreeView.hpp"
#include "Str.hpp"
#include "ui/Nodes.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void fillActor(void* user, uint32_t index, char* dst, uint8_t cap) {
    auto* hud = static_cast<HudContext*>(user);
    if (dst == nullptr || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (hud == nullptr || hud->tool.scene == nullptr || index > 255u) {
        return;
    }
    uint8_t kind = 0;
    sceneFrameActorName(hud->tool.scene, static_cast<uint8_t>(index), dst, cap, &kind);
}

void clickActor(void* user, uint32_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr || index > 255u) {
        return;
    }
    const auto actor = static_cast<uint8_t>(index);
    sceneFrameActorSelect(hud->tool.scene, actor);
    hud->matOpen = 0;
    if (hud->tool.canvas != nullptr && hud->tool.canvas->state != nullptr) {
        uiPropShowMask(*hud->tool.canvas->state, sceneFrameActorSections(hud->tool.scene, actor));
    }
}

void hudRefreshActors(HudContext& hud) {
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr || hud.tool.objects == kUiNone) {
        return;
    }
    const uint32_t count = hud.tool.scene != nullptr ? sceneFrameActorCount(hud.tool.scene) : 0u;
    uiVirtualSource(*hud.tool.canvas->state, hud.tool.objects, count, fillActor, clickActor, &hud);
}

void paintTools(HudContext& hud) {
    if (hud.tool.canvas == nullptr || hud.tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *hud.tool.canvas->state;
    const uint8_t modes[5] = {3, 0, 1, 2, 4};
    const char* btnName[5] = {"toolBtn0", "toolBtn1", "toolBtn2", "toolBtn3", "toolBtn4"};
    for (int i = 0; i < 5; ++i) {
        uiToolButtonState(s, uiFindName(s, btnName[i]), modes[i] == hud.toolMode);
    }
    for (int i = 0; i < 3; ++i) {
        const bool on = hud.snapOn[i] != 0;
        uiToolButtonState(s, hud.snapBtn[i], on);
        uiSetAlpha(s, hud.snapBtn[i], on ? 0.18f : 0.0f);
    }
}

void cmdGizmo(void* user, uint16_t mode) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr) {
        return;
    }
    const auto tool = static_cast<uint8_t>(mode);
    sceneFrameActorGizmo(hud->tool.scene, tool);
    hud->toolMode = tool;
    paintTools(*hud);
}

void cmdSpace(void* user, uint16_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->space = static_cast<uint8_t>(index);
    if (hud->tool.scene != nullptr) {
        sceneFrameGizmoSpace(hud->tool.scene, hud->space);
    }
}

void cmdSnapToggle(void* user, uint16_t channel) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || channel > 2) {
        return;
    }
    const auto axis = static_cast<uint8_t>(channel);
    hud->snapOn[axis] = hud->snapOn[axis] == 0 ? 1 : 0;
    paintTools(*hud);
    if (hud->tool.scene == nullptr) {
        return;
    }
    sceneFrameSnap(hud->tool.scene, axis, hud->snapOn[axis], hud->snapSent[axis]);
}

void applySnap(HudContext& hud, uint8_t channel, uint16_t index) {
    const float moveSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    const float rotSteps[] = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    const float scaleSteps[] = {0.1f, 0.25f, 0.5f, 1.0f};
    const float* table[] = {moveSteps, rotSteps, scaleSteps};
    const uint8_t count[] = {7, 6, 4};
    if (channel > 2 || index >= count[channel]) {
        return;
    }
    const float step = table[channel][index];
    hud.snapPick[channel] = static_cast<uint8_t>(index);
    hud.snapSent[channel] = step;
    if (hud.tool.canvas != nullptr) {
        setSlide(*hud.tool.canvas, hud.snapNum[channel], step);
    }
    if (hud.tool.scene != nullptr) {
        sceneFrameSnap(hud.tool.scene, channel, hud.snapOn[channel], step);
    }
}

void cmdSnapMove(void* user, uint16_t index) {
    if (auto* hud = static_cast<HudContext*>(user)) {
        applySnap(*hud, 0, index);
    }
}

void cmdSnapTurn(void* user, uint16_t index) {
    if (auto* hud = static_cast<HudContext*>(user)) {
        applySnap(*hud, 1, index);
    }
}

void cmdSnapScale(void* user, uint16_t index) {
    if (auto* hud = static_cast<HudContext*>(user)) {
        applySnap(*hud, 2, index);
    }
}

void cmdPivot(void* user, uint16_t index) {
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
    sceneFrameActorSelect(hud.tool.scene, index);
    hud.matOpen = 0;
    hudRefreshActors(hud);
    if (hud.tool.canvas != nullptr && hud.tool.canvas->state != nullptr) {
        uiPropShowMask(*hud.tool.canvas->state, sceneFrameActorSections(hud.tool.scene, index));
    }
}

void cmdSpawn(void* user, uint16_t kind) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr) {
        return;
    }
    spawnAndSelectActor(*hud, sceneFrameSpawn(hud->tool.scene, static_cast<uint8_t>(kind)));
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
        uiSetIcon(s, hud->playGlyph, UiIcon::Stop);
    } else {
        sceneFrameStop(hud->tool.scene);
        uiShow(s, hud->pauseBtn, false);
        uiSetIcon(s, hud->playGlyph, UiIcon::Play);
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

void onViewButton(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    hud->viewWant = hud->viewWant == 2 ? 0 : 1;
}

void onSpawnCube(void* user, uint16_t) {
    cmdSpawn(user, 0);
}

void onDuplicate(void* user, uint16_t) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.scene == nullptr || hud->tool.canvas == nullptr) {
        return;
    }
    spawnAndSelectActor(*hud, sceneFrameActorDuplicate(hud->tool.scene));
}

} // namespace burnhope
