#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "platform/PlatformUtil.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "render/SceneFrame.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void setSlide(Canvas& canvas, uint16_t id, float value) {
    if (canvas.state == nullptr || id == kUiNone) {
        return;
    }
    uiRangeSet(*canvas.state, id, value);
}


float slideAt(const Canvas& canvas, uint16_t id, float fallback) {
    float value = fallback;
    float minV = 0.0f;
    float maxV = 1.0f;
    if (canvas.state == nullptr || !uiRange(*canvas.state, id, value, minV, maxV)) {
        return fallback;
    }
    return value;
}

bool checkOn(const Canvas& canvas, uint16_t id, bool fallback) {
    const int on = canvas.state != nullptr ? uiCheckOn(*canvas.state, id) : -1;
    return on < 0 ? fallback : on != 0;
}

void pullProp(HudContext& hud, Canvas& canvas, uint8_t index) {
    ActorPose pose{};
    ActorPbr pbr{};
    pbr.base[0] = pbr.base[1] = pbr.base[2] = 1.0f;
    pbr.roughness = 0.5f;
    (void)sceneFrameActorPose(hud.tool.scene, index, pose);
    (void)sceneFrameActorPbr(hud.tool.scene, index, pbr);
    if (canvas.state == nullptr) {
        return;
    }
    UiState& state = *canvas.state;
    auto put = [&](uint16_t index, float value) { hud.bind.write(state, index, value); };
    put(kScColor0, pbr.base[0]);
    put(kScColor1, pbr.base[1]);
    put(kScColor2, pbr.base[2]);
    put(kScRough, pbr.roughness);
    put(kScMetal, pbr.metallic);
    put(kScEmissive, pbr.emissive);
    put(kScPos0, pose.pos[0]);
    put(kScPos1, pose.pos[1]);
    put(kScPos2, pose.pos[2]);
    put(kScEuler0, pose.euler[0]);
    put(kScEuler1, pose.euler[1]);
    put(kScEuler2, pose.euler[2]);
    put(kScScale0, pose.scale[0]);
    put(kScScale1, pose.scale[1]);
    put(kScScale2, pose.scale[2]);
    if (kFlActorStatic < 64u) {
        hud.block.flags[kFlActorStatic] = pose.isStatic;
    }
    uiBindCheck(state, hud.bind.idOf(kFlActorStatic, kBindFlag), 0, 0, pose.isStatic != 0);
    hud.actor.isStatic = pose.isStatic;
    hud.propStatic = pose.isStatic;
    hud.propLast[0] = pbr.base[0];
    hud.propLast[1] = pbr.base[1];
    hud.propLast[2] = pbr.base[2];
    hud.propLast[3] = pbr.roughness;
    hud.propLast[4] = pbr.metallic;
    hud.propLast[5] = pbr.emissive;
    hud.propLast[6] = pose.pos[0];
    hud.propLast[7] = pose.pos[1];
    hud.propLast[8] = pose.pos[2];
    hud.propLast[9] = pose.euler[0];
    hud.propLast[10] = pose.euler[1];
    hud.propLast[11] = pose.euler[2];
    hud.propLast[12] = pose.scale[0];
    hud.propLast[13] = pose.scale[1];
    hud.propLast[14] = pose.scale[2];
    float pivot[3]{};
    (void)sceneFrameActorPivot(hud.tool.scene, index, pivot);
    put(kScPivot0, pivot[0]);
    put(kScPivot1, pivot[1]);
    put(kScPivot2, pivot[2]);
    hud.propPivot[0] = pivot[0];
    hud.propPivot[1] = pivot[1];
    hud.propPivot[2] = pivot[2];
    hud.propSel = index;
}

void syncProp(Engine& e, Canvas& canvas) {
    if (e.scene == nullptr) {
        return;
    }
    const uint8_t selected = sceneFrameActorSelected(e.scene);
    if (selected >= sceneFrameActorCount(e.scene)) {
        e.hud.propSel = 0xff;
        return;
    }
    const bool prop = sceneFrameActorKind(e.scene, selected) == static_cast<uint8_t>(SceneActorKind::Prop);
    if (e.hud.propSel != selected) {
        pullProp(e.hud, canvas, selected);
        return;
    }
    if (canvas.state == nullptr) {
        return;
    }
    UiState& state = *canvas.state;
    HudActor& actor = e.hud.actor;
    float cur[18] = {
        actor.color[0], actor.color[1], actor.color[2], actor.rough, actor.metal, actor.emissive,
        actor.pos[0], actor.pos[1], actor.pos[2], actor.euler[0], actor.euler[1], actor.euler[2],
        actor.scale[0], actor.scale[1], actor.scale[2], actor.pivot[0], actor.pivot[1], actor.pivot[2],
    };
    bool pivotMoved = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(cur[15 + axis] - e.hud.propPivot[axis]) > 0.0005f) {
            pivotMoved = true;
        }
    }
    if (pivotMoved) {
        float piv[3] = {cur[15], cur[16], cur[17]};
        sceneFrameActorSetPivot(e.scene, selected, piv);
        for (int axis = 0; axis < 3; ++axis) {
            e.hud.propPivot[axis] = cur[15 + axis];
        }
    } else {
        float got[3]{};
        if (sceneFrameActorPivot(e.scene, selected, got)) {
            bool driftPivot = false;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::fabs(got[axis] - e.hud.propPivot[axis]) > 0.0005f) {
                    driftPivot = true;
                }
            }
            if (driftPivot) {
                e.hud.bind.write(state, kScPivot0, got[0]);
                e.hud.bind.write(state, kScPivot1, got[1]);
                e.hud.bind.write(state, kScPivot2, got[2]);
                for (int axis = 0; axis < 3; ++axis) {
                    e.hud.propPivot[axis] = got[axis];
                    cur[15 + axis] = got[axis];
                }
            }
        }
    }
    bool moved = actor.isStatic != e.hud.propStatic;
    for (int i = prop ? 0 : 6; i < 15; ++i) {
        if (std::fabs(cur[i] - e.hud.propLast[i]) > 0.0005f) {
            moved = true;
        }
    }
    if (moved) {
        ActorPbr out{};
        out.base[0] = cur[0];
        out.base[1] = cur[1];
        out.base[2] = cur[2];
        out.roughness = cur[3];
        out.metallic = cur[4];
        out.emissive = cur[5];
        ActorPose pose{};
        pose.pos[0] = cur[6];
        pose.pos[1] = cur[7];
        pose.pos[2] = cur[8];
        pose.euler[0] = cur[9];
        pose.euler[1] = cur[10];
        pose.euler[2] = cur[11];
        pose.scale[0] = cur[12];
        pose.scale[1] = cur[13];
        pose.scale[2] = cur[14];
        pose.isStatic = actor.isStatic;
        if (prop) {
            sceneFrameActorSetPbr(e.scene, selected, out);
        }
        sceneFrameActorSetPose(e.scene, selected, pose);
        e.hud.propStatic = pose.isStatic;
        for (int i = 0; i < 15; ++i) {
            e.hud.propLast[i] = cur[i];
        }
        return;
    }
    ActorPose pose{};
    if (!sceneFrameActorPose(e.scene, selected, pose)) {
        return;
    }
    const float got[9] = {
        pose.pos[0], pose.pos[1], pose.pos[2], pose.euler[0], pose.euler[1], pose.euler[2],
        pose.scale[0], pose.scale[1], pose.scale[2],
    };
    bool drift = false;
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(got[i] - e.hud.propLast[6 + i]) > 0.0005f) {
            drift = true;
        }
    }
    if (!drift) {
        return;
    }
    e.hud.bind.write(state, kScPos0, pose.pos[0]);
    e.hud.bind.write(state, kScPos1, pose.pos[1]);
    e.hud.bind.write(state, kScPos2, pose.pos[2]);
    e.hud.bind.write(state, kScEuler0, pose.euler[0]);
    e.hud.bind.write(state, kScEuler1, pose.euler[1]);
    e.hud.bind.write(state, kScEuler2, pose.euler[2]);
    e.hud.bind.write(state, kScScale0, pose.scale[0]);
    e.hud.bind.write(state, kScScale1, pose.scale[1]);
    e.hud.bind.write(state, kScScale2, pose.scale[2]);
    for (int i = 0; i < 9; ++i) {
        e.hud.propLast[6 + i] = got[i];
    }
}

} // namespace burnhope
