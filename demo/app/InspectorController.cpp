#include "app/InspectorController.hpp"

#include "app/EditorState.hpp"
#include "gui/GuiHost.hpp"
#include "gpu_scene/SceneObject.hpp"

#include <array>

namespace burnhope {
namespace {

constexpr const char* kPbrKey[6] = {"actor.mat.base.r", "actor.mat.base.g", "actor.mat.base.b", "actor.mat.rough", "actor.mat.metal", "actor.mat.emissive"};

constexpr const char* kPoseKey[9] = {"actor.pos.x", "actor.pos.y", "actor.pos.z", "actor.rot.x", "actor.rot.y", "actor.rot.z",
                                      "actor.scl.x", "actor.scl.y", "actor.scl.z"};

// Every node is addressed by its JSON "bind" key (assets/gui/inspector_*.json), never by its place in the layout.
struct Slot {
    const char* key;
    float* value;
};

// On/Off buttons: key and action are fixed; the state lives in a tweak word or a flag byte (see toggleRef).
struct ToggleDef {
    uint32_t action;
    const char* key;
};

constexpr uint32_t kToggleCount = 9;
constexpr ToggleDef kToggle[kToggleCount] = {
    {kActSunOn, "light.sunOn"},   {kActSunCast, "light.sunCast"}, {kActSunCsm, "light.sunCsm"},
    {kActSky, "light.sky"},       {kActClouds, "light.clouds"},   {kActClockSun, "light.clock"},
    {kActBloom, "post.bloomOn"},  {kActGtao, "post.aoOn"},        {kActFog, "post.fogOn"},
};
constexpr uint32_t kPoseToggle = 15;  // lastToggle index of the "actor.static" button

// Exactly one of word (tweaks) / byte (flags) is set.
struct ToggleRef {
    int* word;
    uint8_t* byte;
};

ToggleRef toggleRef(EditorState& ed, uint32_t i) {
    SceneTweaks& t = ed.tweaks;
    switch (i) {
    case 0: return {&t.sunEnabled, nullptr};
    case 1: return {&t.sunCast, nullptr};
    case 2: return {&t.sunCsm, nullptr};
    case 3: return {nullptr, &ed.flags.sky};
    case 4: return {nullptr, &ed.flags.clouden};
    case 5: return {&t.clockSun, nullptr};
    case 6: return {nullptr, &ed.flags.bloomen};
    case 7: return {nullptr, &ed.flags.aoMode};
    default: return {nullptr, &ed.flags.fogen};
    }
}

std::array<Slot, 20> slotsOf(EditorState& ed) {
    HudPost& p = ed.post;
    SceneTweaks& t = ed.tweaks;
    return {{{"light.sunGain", &p.sunGain}, {"light.sunZenith", &t.sunZenith}, {"light.sunAzimuth", &t.sunAzimuth},
             {"light.sunKelvin", &t.sunKelvin}, {"light.sunColor.r", &p.sunR}, {"light.sunColor.g", &p.sunG},
             {"light.sunColor.b", &p.sunB}, {"light.cloudCover", &t.cloudCoverage}, {"light.cloudDensity", &t.cloudDensity},
             {"light.timeOfDay", &t.timeOfDay}, {"post.bloom", &p.bloom}, {"post.bloomThreshold", &t.bloomThreshold},
             {"post.gtao", &p.gtao}, {"post.gtaoRadius", &t.gtaoRadius}, {"post.exposure", &p.exposure},
             {"post.contrast", &p.contrast}, {"post.saturation", &p.saturation}, {"post.vignette", &p.vignette},
             {"post.grain", &p.grain}, {"post.fogDensity", &t.fogDensity}}};
}

float* poseSlot(ActorPose& pose, uint32_t field) {
    return field < 3 ? &pose.pos[field] : (field < 6 ? &pose.euler[field - 3] : &pose.scale[field - 6]);
}

float* pbrSlot(ActorPbr& pbr, uint32_t field) {
    return field < 3 ? &pbr.base[field] : (field == 3 ? &pbr.roughness : (field == 4 ? &pbr.metallic : &pbr.emissive));
}

uint8_t selectedActor(const InspectorState& ins) {
    const uint8_t s = sceneFrameActorSelected(ins.scene);
    return s < sceneFrameActorCount(ins.scene) ? s : kSceneActorNone;
}

void setFloat(void* user, float value) {
    *static_cast<float*>(user) = value;
}

void onPose(void* user, float value) {
    const auto* field = static_cast<const InspectorField*>(user);
    const uint8_t a = selectedActor(*field->state);
    ActorPose pose{};
    if (a != kSceneActorNone && sceneFrameActorPose(field->state->scene, a, pose)) {
        *poseSlot(pose, field->index) = value;
        sceneFrameActorSetPose(field->state->scene, a, pose);
    }
}

void pushSwatch(InspectorState& ins, const ActorPbr& pbr) {
    guiHostSetColor(ins.gui, "actor.mat.preview", pbr.base[0], pbr.base[1], pbr.base[2]);
}

void onPbr(void* user, float value) {
    const auto* field = static_cast<const InspectorField*>(user);
    InspectorState& ins = *field->state;
    const uint8_t a = selectedActor(ins);
    ActorPbr pbr{};
    if (a != kSceneActorNone && sceneFrameActorPbr(ins.scene, a, pbr)) {
        *pbrSlot(pbr, field->index) = value;
        sceneFrameActorSetPbr(ins.scene, a, pbr);
        pushSwatch(ins, pbr);
    }
}

void onClick(void* user, uint32_t action) {
    auto& ins = *static_cast<InspectorState*>(user);
    for (uint32_t i = 0; i < kToggleCount; ++i) {
        if (kToggle[i].action == action) {
            const ToggleRef t = toggleRef(*ins.editor, i);
            if (t.word != nullptr) {
                *t.word = *t.word != 0 ? 0 : 1;
            } else {
                *t.byte = *t.byte != 0 ? 0 : 1;
            }
            return;
        }
    }
    const uint8_t a = selectedActor(ins);
    ActorPose pose{};
    if (a == kSceneActorNone || !sceneFrameActorPose(ins.scene, a, pose)) {
        return;
    }
    switch (action) {
    case kActStatic: pose.isStatic = pose.isStatic != 0 ? 0 : 1; break;
    case kActResetPos: pose.pos[0] = pose.pos[1] = pose.pos[2] = 0.0f; break;
    case kActResetRot: pose.euler[0] = pose.euler[1] = pose.euler[2] = 0.0f; break;
    case kActResetScale: pose.scale[0] = pose.scale[1] = pose.scale[2] = 1.0f; break;
    default: return;
    }
    sceneFrameActorSetPose(ins.scene, a, pose);
}

void showToggle(InspectorState& ins, const char* key, uint8_t& last, bool on) {
    if (last == static_cast<uint8_t>(on)) {
        return;
    }
    last = static_cast<uint8_t>(on);
    guiHostSetText(ins.gui, key, on ? "On" : "Off");
    guiHostSetActive(ins.gui, key, on);
}

void followSelection(InspectorState& ins, uint8_t actor) {
    char name[24] = "No selection";
    uint8_t kind = 0;
    if (actor != kSceneActorNone) {
        sceneFrameActorName(ins.scene, actor, name, sizeof(name), &kind);
    }
    const uint8_t sections = actor != kSceneActorNone ? sceneFrameActorSections(ins.scene, actor) : 0;
    guiHostSetText(ins.gui, "inspector.actor", name);
    guiHostSetShown(ins.gui, "inspector.xform", (sections & kSecTransform) != 0);
    guiHostSetShown(ins.gui, "inspector.mat", (sections & kSecMaterial) != 0);
    ActorPbr pbr{};
    if ((sections & kSecMaterial) != 0 && sceneFrameActorPbr(ins.scene, actor, pbr)) {
        for (uint32_t i = 0; i < 6; ++i) {
            guiHostSetValue(ins.gui, kPbrKey[i], *pbrSlot(pbr, i));
        }
        pushSwatch(ins, pbr);
    }
}

} // namespace

bool inspectorBind(InspectorState& ins, GuiHost& gui, SceneFrame* scene, EditorState& editor) {
    ins.gui = &gui;
    ins.scene = scene;
    ins.editor = &editor;
    InspectorState& st = ins;
    bool ok = true;
    for (const Slot& s : slotsOf(*ins.editor)) {
        ok = guiHostBindDrag(ins.gui, s.key, setFloat, s.value) && ok;
        guiHostSetValue(ins.gui, s.key, *s.value);
    }
    for (uint32_t i = 0; i < 9; ++i) {
        st.pose[i] = {&ins, i};
        ok = guiHostBindDrag(ins.gui, kPoseKey[i], onPose, &st.pose[i]) && ok;
    }
    for (uint32_t i = 0; i < 6; ++i) {
        st.pbr[i] = {&ins, i};
        ok = guiHostBindDrag(ins.gui, kPbrKey[i], onPbr, &st.pbr[i]) && ok;
    }
    for (const ToggleDef& t : kToggle) {
        ok = guiHostBindClick(ins.gui, t.action, onClick, &ins) && ok;
    }
    for (uint32_t action : {kActStatic, kActResetPos, kActResetRot, kActResetScale}) {
        ok = guiHostBindClick(ins.gui, action, onClick, &ins) && ok;
    }
    for (uint8_t& last : st.lastToggle) {
        last = 0xff;
    }
    return ok;
}

void inspectorSync(InspectorState& ins) {
    InspectorState& st = ins;
    const uint8_t actor = selectedActor(ins);
    if (actor != st.shown) {
        st.shown = actor;
        followSelection(ins, actor);
    }
    ActorPose pose{};
    if (actor != kSceneActorNone && sceneFrameActorPose(ins.scene, actor, pose)) {
        for (uint32_t i = 0; i < 9; ++i) {
            guiHostSetValue(ins.gui, kPoseKey[i], *poseSlot(pose, i));
        }
        showToggle(ins, "actor.static", st.lastToggle[kPoseToggle], pose.isStatic != 0);
    }
    for (uint32_t i = 0; i < kToggleCount; ++i) {
        const ToggleRef t = toggleRef(*ins.editor, i);
        showToggle(ins, kToggle[i].key, st.lastToggle[i], t.word != nullptr ? *t.word != 0 : *t.byte != 0);
    }
}

} // namespace burnhope
