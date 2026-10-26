#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/SceneBlockFill.hpp"

#include "Str.hpp"

#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Popover.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/Text.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void fillGpuStat(void* user, uint32_t index, char* dst, uint8_t cap) {
    auto* hud = static_cast<HudContext*>(user);
    if (dst == nullptr || cap == 0) {
        return;
    }
    const char* text = hud != nullptr && hud->scene != nullptr ? sceneFrameGpuText(hud->scene) : nullptr;
    strCopyLine(text, index, dst, cap);
}

void placeStats(UiState& state, const HudContext& hud) {
    if (hud.overlay >= state.count) {
        return;
    }
    const uint16_t origin = uiParent(state, hud.overlay);
    const UiBox root = uiNodeBox(state, origin);
    const UiBox view = uiNodeBox(state, hud.viewPane);
    const UiBox bar = uiNodeBox(state, hud.toolBar);
    float x = 8.0f;
    float y = 80.0f;
    if (view.w > 1.0f) {
        x = view.x + 8.0f - root.x;
    }
    if (bar.h > 1.0f) {
        y = bar.y + bar.h + 8.0f - root.y;
    }
    uiPlaceAbsolute(state, hud.overlay, x, y);
}

void syncViewport(Engine& e, Canvas& canvas) {
    SceneFrame* scene = e.scene;
    UiState* state = canvas.state;
    if (scene == nullptr || state == nullptr || e.hud.camFov == kUiNone) {
        return;
    }
    HudContext& hud = e.hud;
    if (hud.camInit == 0) {
        const float fov = sceneFrameCameraFov(scene);
        const float nearZ = sceneFrameCameraNear(scene);
        const float farZ = sceneFrameCameraFar(scene);
        const float speed = sceneFrameCameraSpeed(scene);
        uiSliderSet(*state, hud.camFov, fov);
        setSlide(canvas, hud.camFovNum, fov);
        setSlide(canvas, hud.camNear, nearZ);
        setSlide(canvas, hud.camFar, farZ);
        setSlide(canvas, hud.camSpeed, speed);
        hud.fovSent = fov;
        hud.nearSent = nearZ;
        hud.farSent = farZ;
        hud.speedSent = speed;
        hud.camInit = 1;
    }
    const float fovSlide = slideAt(canvas, hud.camFov, hud.fovSent);
    const float fovNum = slideAt(canvas, hud.camFovNum, hud.fovSent);
    float fov = hud.fovSent;
    if (std::fabs(fovSlide - hud.fovSent) > 0.05f) {
        fov = fovSlide;
    } else if (std::fabs(fovNum - hud.fovSent) > 0.05f) {
        fov = fovNum;
    }
    const float nearZ = slideAt(canvas, hud.camNear, hud.nearSent);
    const float farZ = slideAt(canvas, hud.camFar, hud.farSent);
    if (std::fabs(fov - hud.fovSent) > 0.05f || std::fabs(nearZ - hud.nearSent) > 0.0005f || std::fabs(farZ - hud.farSent) > 0.5f) {
        sceneFrameCamera(scene, fov, nearZ, farZ);
        hud.fovSent = sceneFrameCameraFov(scene);
        hud.nearSent = sceneFrameCameraNear(scene);
        hud.farSent = sceneFrameCameraFar(scene);
        uiSliderSet(*state, hud.camFov, hud.fovSent);
        setSlide(canvas, hud.camFovNum, hud.fovSent);
        setSlide(canvas, hud.camNear, hud.nearSent);
        setSlide(canvas, hud.camFar, hud.farSent);
    }
    const float liveSpeed = sceneFrameCameraSpeed(scene);
    const float shownSpeed = slideAt(canvas, hud.camSpeed, liveSpeed);
    if (std::fabs(liveSpeed - hud.speedSent) > 0.02f && std::fabs(shownSpeed - hud.speedSent) < 0.02f) {
        setSlide(canvas, hud.camSpeed, liveSpeed);
        hud.speedSent = liveSpeed;
    } else if (std::fabs(shownSpeed - hud.speedSent) > 0.001f) {
        sceneFrameCameraSpeed(scene, shownSpeed);
        hud.speedSent = sceneFrameCameraSpeed(scene);
        setSlide(canvas, hud.camSpeed, hud.speedSent);
    }
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const float shown = slideAt(canvas, hud.snapNum[channel], hud.snapSent[channel]);
        if (std::fabs(shown - hud.snapSent[channel]) > 0.0001f) {
            hud.snapSent[channel] = shown;
            sceneFrameSnap(scene, channel, hud.snapOn[channel], shown);
        }
    }
    const uint8_t proj = checkOn(canvas, hud.camProj[1], false) ? 1 : 0;
    if (proj != sceneFrameCameraProject(scene)) {
        sceneFrameCameraProject(scene, proj);
    }
    if (hud.camBreak != 0) {
        hud.camBreak = 0;
        if (sceneFrameCameraMode(scene) != 0 && state != nullptr) {
            sceneFrameCameraRelease(scene);
            for (uint8_t i = 0; i < 6; ++i) {
                if (hud.camAxis[i] < state->count) {
                    uiCheckAssign(*state, hud.camAxis[i], 0);
                }
            }
            hud.viewSent = 0;
            uiNotifyVisualDirty(*state);
        }
    }
    uint8_t view = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        if (checkOn(canvas, hud.camAxis[i], false)) {
            view = static_cast<uint8_t>(i + 1);
        }
    }
    if (view != hud.viewSent) {
        sceneFrameCameraView(scene, view);
        hud.viewSent = view;
    }
    if (hud.camPanel == kUiNone) {
        return;
    }
    uiSyncPopover(*state, hud.camPanel, hud.camBtn, 400.0f, 500.0f, hud.camWant);
    const float viewW = uiStyleWidth(*state, hud.viewPanel, 160.0f);
    uiSyncPopover(*state, hud.viewPanel, hud.viewBtn, viewW, hud.viewPopH, hud.viewWant);
    bool anyStat = false;
    for (uint8_t i = 0; i < 6; ++i) {
        hud.statOn[i] = checkOn(canvas, hud.statCheck[i], false) ? 1 : 0;
        uiShow(*state, hud.statLine[i], hud.statOn[i] != 0);
        anyStat = anyStat || hud.statOn[i] != 0;
    }
    uiShow(*state, hud.overlay, anyStat);
    if (anyStat && hud.overlay < state->count) {
        HostMeters meters{};
        platformHostMeters(hud.sampler, meters);
        const double ram = static_cast<double>(meters.rss) / (1024.0 * 1024.0);
        const double vram = static_cast<double>(hostGpuMemoryUsed(e.host)) / (1024.0 * 1024.0);
        const float fps = sceneFrameFps(scene);
        const float frame = fps > 1.0f ? 1000.0f / fps : 0.0f;
        char line[6][64]{};
        std::snprintf(line[0], sizeof(line[0]), "FPS %.0f   %.2f ms", static_cast<double>(fps), static_cast<double>(frame));
        std::snprintf(line[1], sizeof(line[1]), "GPU %.2f ms", static_cast<double>(sceneFrameGpuMs(scene)));
        std::snprintf(line[2], sizeof(line[2]), "Draws %u   Tris %u", sceneFrameDraws(scene), sceneFrameTriangles(scene));
        std::snprintf(line[3], sizeof(line[3]), "Physics %.2f ms", static_cast<double>(sceneFramePhysicsMs(scene)));
        std::snprintf(line[4], sizeof(line[4]), "RAM %.0f MB", ram);
        std::snprintf(line[5], sizeof(line[5]), "VRAM %.0f MB", vram);
        for (uint8_t i = 0; i < 6; ++i) {
            if (hud.statOn[i] != 0) {
                uiText(*state, hud.statLine[i], line[i]);
            }
        }
        placeStats(*state, hud);
    }
    uiSyncPopover(*state, hud.statsPanel, hud.gearBtn, 280.0f, 252.0f, hud.statsWant);
}


void hudSeed(HudContext& hud) {
    hud.post.grain = 0.0f;
    hud.post.vignette = 0.35f;
    hud.post.gtao = 1.0f;
    hud.tweaks.sscsReach = 1.0f;
    hud.tweaks.sscsThick = 0.02f;
    hud.tweaks.sscsSteps = 8.0f;
}

void hudPushFrame(Engine& e, Canvas& hud, uint8_t weatherIndex, uint8_t selected) {
    HudContext& h = e.hud;
    const HudFlags& f = h.flags;
    sceneFrameDebug(e.scene, f.hiz != 0, f.cone != 0, f.lod != 0, f.frust != 0, f.small != 0, f.shadowCull != 0, f.backf != 0, f.scrn != 0, f.behind != 0, f.viewMode);
    HudPost post = h.post;
    float grade = 0.0f;
    if (f.aa == 5) {
        grade += 1.0f;
    }
    if (f.lumScale != 0) {
        grade += 2.0f;
    }
    if (f.gamut != 0) {
        grade += 4.0f;
    }
    if (f.aa == 4) {
        grade += 8.0f;
    }
    post.gradeFlags = grade;
    if (hud.state != nullptr && f.wxClock != 0) {
        const float zenith = sceneFrameWeatherZenith(e.scene);
        const float azimuth = sceneFrameWeatherAzimuth(e.scene);
        h.bind.write(*hud.state, kScSunZenith, zenith);
        h.bind.write(*hud.state, kScSunAzimuth, azimuth);
        if (weatherIndex != kSceneActorNone) {
            sceneFrameActorSetEuler(e.scene, weatherIndex, zenith, azimuth);
        }
    } else if (weatherIndex != kSceneActorNone && hud.state != nullptr) {
        float pitch = h.tweaks.sunZenith;
        float yaw = h.tweaks.sunAzimuth;
        sceneFrameActorEuler(e.scene, weatherIndex, pitch, yaw);
        const bool dragging = sceneFrameActorDragging(e.scene) && selected == weatherIndex;
        if (dragging || (std::fabs(h.tweaks.sunZenith - pitch) <= 0.25f && std::fabs(h.tweaks.sunAzimuth - yaw) <= 0.25f)) {
            h.bind.write(*hud.state, kScSunZenith, pitch);
            h.bind.write(*hud.state, kScSunAzimuth, yaw);
        } else {
            sceneFrameActorSetEuler(e.scene, weatherIndex, h.tweaks.sunZenith, h.tweaks.sunAzimuth);
        }
    }
    sceneBlockApply(h);
    SceneTweaks tweaks = h.tweaks;
    tweaks.forceLod = static_cast<int32_t>(f.forceLod);
    if (f.reflectMode != 2) {
        tweaks.ssrRt = 0.0f;
    }
    tweaks.clockSun = f.wxClock;
    tweaks.sunEnabled = f.sun;
    tweaks.sunCast = f.sunCast;
    tweaks.sunCsm = f.sunShadow == 1 ? 1 : 0;
    tweaks.fsrQuality = f.fsrQ;
    tweaks.lightProfile = f.giMode;
    tweaks.tonemapper = f.tone;
    sceneBlockFill(h.block, post, tweaks, f, h.actor);
    sceneFrameSetBlock(e.scene, h.block);
    syncViewport(e, hud);
    const float shownTime = sceneFrameWeatherTime(e.scene);
    if (shownTime >= 0.0f && hud.state != nullptr) {
        h.bind.write(*hud.state, kScTimeOfDay, shownTime);
        if (tweaks.clockSun != 0 && weatherIndex != kSceneActorNone) {
            const float zen = sceneFrameWeatherZenith(e.scene);
            const float az = sceneFrameWeatherAzimuth(e.scene);
            h.bind.write(*hud.state, kScSunZenith, zen);
            h.bind.write(*hud.state, kScSunAzimuth, az);
            sceneFrameActorSetEuler(e.scene, weatherIndex, zen, az);
        }
    }
    if (hud.state != nullptr && e.hud.ids.statList != kUiNone) {
        const uint32_t lines = strCountLines(sceneFrameGpuText(e.scene));
        uiVirtualSource(*hud.state, e.hud.ids.statList, lines, fillGpuStat, nullptr, &e.hud);
    }
}

void hudFrame(Engine& e) {
    if (e.scene == nullptr || e.host.primary < 0) {
        return;
    }
    char fps[96];
    sceneFrameStats(e.scene, fps, sizeof(fps));
    HostMeters meters{};
    platformHostMeters(e.hud.sampler, meters);
    const uint64_t vram = hostGpuMemoryUsed(e.host);
    char memLine[96];
    char loadLine[96];
    std::snprintf(memLine, sizeof(memLine), "vram %.0f МБ   ram %.0f МБ", static_cast<double>(vram) / (1024.0 * 1024.0), static_cast<double>(meters.rss) / (1024.0 * 1024.0));
    if (meters.gpu >= 0.0f) {
        std::snprintf(loadLine, sizeof(loadLine), "cpu %.0f%%   gpu %.0f%%", meters.cpu, meters.gpu);
    } else {
        std::snprintf(loadLine, sizeof(loadLine), "cpu %.0f%%   gpu n/a", meters.cpu);
    }
    Canvas& hud = e.host.view[e.host.primary].canvas;
    if (hud.state != nullptr) {
        uiText(*hud.state, e.hud.ids.statsLbl, fps);
        uiText(*hud.state, e.hud.ids.memLbl, memLine);
        uiText(*hud.state, e.hud.ids.loadLbl, loadLine);
    }
    const uint8_t selected = sceneFrameActorSelected(e.scene);
    if (selected != e.hud.shown && selected != kSceneActorNone) {
        e.hud.matOpen = 0;
        e.hud.shown = selected;
        if (hud.state != nullptr) {
            uiPropShowMask(*hud.state, sceneFrameActorSections(e.scene, selected));
            scrollReset(*hud.state, e.hud.tool.propPage);
        }
        sceneFrameActorGizmo(e.scene, e.hud.toolMode);
    }
    if (hud.state != nullptr) {
        e.hud.bind.sync(*hud.state);
        e.hud.bind.syncSwatches(*hud.state);
        sceneBlockApply(e.hud);
        hudPaintMaterial(e.hud);
        hudMaterialSync(e, hud);
        hudMaterialDrag(e.hud, *hud.state);
        saveOpenMaterial(e.hud);
        hudBakeMaterialView(e.hud);
        hudShowBodies(e.hud, *hud.state);
    }
    syncProp(e, hud);
    uint8_t weatherIndex = kSceneActorNone;
    const uint8_t count = sceneFrameActorCount(e.scene);
    for (uint8_t i = 0; i < count; ++i) {
        if (sceneFrameActorKind(e.scene, i) == static_cast<uint8_t>(SceneActorKind::Weather)) {
            weatherIndex = i;
            break;
        }
    }
    hudPushFrame(e, hud, weatherIndex, selected);
}

} // namespace burnhope
