#include "app/HudBuild.hpp"

#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "render/Sharc.hpp"
#include "rhi/Device.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Text.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

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
    UiState* state = canvas.state.get();
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
                    if (auto* check = state->ent[hud.camAxis[i]].try_get_mut<UiCheck>()) {
                        check->on = 0;
                    }
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
    float viewW = 160.0f;
    if (hud.viewPanel < state->count) {
        if (const UiFlex* flex = state->ent[hud.viewPanel].try_get<UiFlex>()) {
            viewW = flex->width;
        }
    }
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
        platformHostMeters(meters);
        const double ram = static_cast<double>(meters.rss) / (1024.0 * 1024.0);
        const double vram = static_cast<double>(deviceMemoryUsed(e.host.device)) / (1024.0 * 1024.0);
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
    sceneFrameTune(e.scene, post);
    if (hud.state != nullptr && f.wxClock != 0) {
        const float zenith = sceneFrameWeatherZenith(e.scene);
        const float azimuth = sceneFrameWeatherAzimuth(e.scene);
        h.bind.write(*hud.state, &h.tweaks.sunZenith, zenith);
        h.bind.write(*hud.state, &h.tweaks.sunAzimuth, azimuth);
        if (weatherIndex != kSceneActorNone) {
            sceneFrameActorSetEuler(e.scene, weatherIndex, zenith, azimuth);
        }
    } else if (weatherIndex != kSceneActorNone && hud.state != nullptr) {
        float pitch = h.tweaks.sunZenith;
        float yaw = h.tweaks.sunAzimuth;
        sceneFrameActorEuler(e.scene, weatherIndex, pitch, yaw);
        const bool dragging = sceneFrameActorDragging(e.scene) && selected == weatherIndex;
        if (dragging || (std::fabs(h.tweaks.sunZenith - pitch) <= 0.25f && std::fabs(h.tweaks.sunAzimuth - yaw) <= 0.25f)) {
            h.bind.write(*hud.state, &h.tweaks.sunZenith, pitch);
            h.bind.write(*hud.state, &h.tweaks.sunAzimuth, yaw);
        } else {
            sceneFrameActorSetEuler(e.scene, weatherIndex, h.tweaks.sunZenith, h.tweaks.sunAzimuth);
        }
    }
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
    tweaks.passMask = 0;
    if (f.cluster != 0) {
        tweaks.passMask |= 1u;
    }
    if (f.probes != 0) {
        tweaks.passMask |= 2u;
    }
    if (f.giMode == 3) {
        tweaks.passMask |= 262144u;
    }
    if (f.aa == 3) {
        tweaks.passMask |= 4194304u;
    } else if (f.aa == 2) {
        tweaks.passMask |= 8u;
    }
    if (f.aoMode == 1) {
        tweaks.passMask |= 16u;
    }
    if (f.bloomen != 0) {
        tweaks.passMask |= 32u;
    }
    if (f.aa == 1) {
        tweaks.passMask |= 4u;
    }
    if (f.sun != 0) {
        tweaks.passMask |= 256u;
    }
    if (f.points != 0) {
        tweaks.passMask |= 512u;
    }
    if (f.spots != 0) {
        tweaks.passMask |= 65536u;
    }
    if (f.area != 0) {
        tweaks.passMask |= 131072u;
    }
    if (f.sky != 0) {
        tweaks.passMask |= 1024u;
    }
    if (f.clouden != 0) {
        tweaks.passMask |= 2048u;
    }
    if (f.fogen != 0) {
        tweaks.passMask |= 4096u;
    }
    if (f.reflectMode != 0) {
        tweaks.passMask |= 8192u;
    }
    if (f.contacten != 0) {
        tweaks.passMask |= 16384u;
    }
    if (f.vt != 0) {
        tweaks.passMask |= 32768u;
    }
    if ((tweaks.passMask & 262144u) != 0u && tweaks.lightProfile == 1u) {
        tweaks.passMask |= 524288u;
    }
    if ((tweaks.passMask & 262144u) != 0u && tweaks.lightProfile == 2u) {
        tweaks.passMask |= 128u;
    }
    if ((tweaks.passMask & 262144u) != 0u && tweaks.lightProfile == 3u) {
        tweaks.passMask |= kPassSharc;
    }
    if (f.directMode == 1) {
        tweaks.passMask |= 1048576u;
        if (tweaks.restirSunOn && tweaks.sunCsm == 0 && (tweaks.passMask & 256u) != 0u) {
            tweaks.passMask |= 2097152u;
        }
    }
    sceneFrameTweaks(e.scene, tweaks);
    syncViewport(e, hud);
    const float shownTime = sceneFrameWeatherTime(e.scene);
    if (shownTime >= 0.0f && hud.state != nullptr) {
        h.bind.write(*hud.state, &h.tweaks.timeOfDay, shownTime);
        if (tweaks.clockSun != 0 && weatherIndex != kSceneActorNone) {
            const float zen = sceneFrameWeatherZenith(e.scene);
            const float az = sceneFrameWeatherAzimuth(e.scene);
            h.bind.write(*hud.state, &h.tweaks.sunZenith, zen);
            h.bind.write(*hud.state, &h.tweaks.sunAzimuth, az);
            sceneFrameActorSetEuler(e.scene, weatherIndex, zen, az);
        }
    }
    const char* gpuText = sceneFrameGpuText(e.scene);
    char lines[40][96]{};
    uint32_t line = 0;
    const char* cursor = gpuText != nullptr ? gpuText : "";
    while (line < 40 && cursor[0] != '\0') {
        const char* nl = std::strchr(cursor, '\n');
        const size_t n = nl != nullptr ? static_cast<size_t>(nl - cursor) : std::strlen(cursor);
        const size_t copy = n < 95 ? n : 95;
        std::memcpy(lines[line], cursor, copy);
        lines[line][copy] = '\0';
        line += 1;
        if (nl == nullptr) {
            break;
        }
        cursor = nl + 1;
    }
    if (hud.state != nullptr) {
        for (uint32_t i = 0; i < 40; ++i) {
            uiText(*hud.state, e.hud.ids.stat[i], lines[i]);
        }
    }
}

} // namespace burnhope
