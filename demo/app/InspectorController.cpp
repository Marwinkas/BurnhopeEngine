#include "app/InspectorController.hpp"

#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "gpu_scene/SceneObject.hpp"

#include <cstdio>

namespace burnhope {
namespace {

constexpr const char* kRoot = "main::inspector::";
constexpr const char* kPoseRow[3] = {"pos", "rot", "scl"};
constexpr const char kAxis[3] = {'x', 'y', 'z'};
constexpr const char* kPbrLeaf[6] = {"base::r", "base::g", "base::b", "rough::v", "metal::v", "emissive::v"};

struct Slot {
    const char* section;
    const char* leaf; // "<row>::<leaf>"
    float* value;
};

// On/Off buttons: exactly one of word (tweaks) / byte (flags) is set.
struct Toggle {
    uint32_t action;
    const char* section;
    const char* row;
    int* word;
    uint8_t* byte;
};

struct Layout {
    Slot slot[20];
    Toggle toggle[9];
};

Layout layoutOf(EditorState& ed) {
    HudPost& p = ed.post;
    SceneTweaks& t = ed.tweaks;
    return {
        {{"light", "sunGain::v", &p.sunGain}, {"light", "sunZenith::v", &t.sunZenith}, {"light", "sunAzimuth::v", &t.sunAzimuth},
         {"light", "sunKelvin::v", &t.sunKelvin}, {"light", "sunColor::r", &p.sunR}, {"light", "sunColor::g", &p.sunG},
         {"light", "sunColor::b", &p.sunB}, {"light", "cloudCover::v", &t.cloudCoverage}, {"light", "cloudDensity::v", &t.cloudDensity},
         {"light", "timeOfDay::v", &t.timeOfDay}, {"post", "bloom::v", &p.bloom}, {"post", "bloomThreshold::v", &t.bloomThreshold},
         {"post", "gtao::v", &p.gtao}, {"post", "gtaoRadius::v", &t.gtaoRadius}, {"post", "exposure::v", &p.exposure},
         {"post", "contrast::v", &p.contrast}, {"post", "saturation::v", &p.saturation}, {"post", "vignette::v", &p.vignette},
         {"post", "grain::v", &p.grain}, {"post", "fogDensity::v", &t.fogDensity}},
        {{kActSunOn, "light", "sunOn", &t.sunEnabled, nullptr}, {kActSunCast, "light", "sunCast", &t.sunCast, nullptr},
         {kActSunCsm, "light", "sunCsm", &t.sunCsm, nullptr}, {kActSky, "light", "sky", nullptr, &ed.flags.sky},
         {kActClouds, "light", "clouds", nullptr, &ed.flags.clouden}, {kActClockSun, "light", "clock", &t.clockSun, nullptr},
         {kActBloom, "post", "bloomOn", nullptr, &ed.flags.bloomen}, {kActGtao, "post", "aoOn", nullptr, &ed.flags.aoMode},
         {kActFog, "post", "fogOn", nullptr, &ed.flags.fogen}},
    };
}

void path(char* out, size_t cap, const char* section, const char* leaf) {
    std::snprintf(out, cap, "%s%sSec::body::%s", kRoot, section, leaf);
}

void posePath(char* out, size_t cap, uint32_t field) {
    std::snprintf(out, cap, "%sxformSec::body::%s::%c", kRoot, kPoseRow[field / 3], kAxis[field % 3]);
}

float* poseSlot(ActorPose& pose, uint32_t field) {
    return field < 3 ? &pose.pos[field] : (field < 6 ? &pose.euler[field - 3] : &pose.scale[field - 6]);
}

float* pbrSlot(ActorPbr& pbr, uint32_t field) {
    return field < 3 ? &pbr.base[field] : (field == 3 ? &pbr.roughness : (field == 4 ? &pbr.metallic : &pbr.emissive));
}

uint8_t selectedActor(const Engine& e) {
    const uint8_t s = sceneFrameActorSelected(e.scene);
    return s < sceneFrameActorCount(e.scene) ? s : kSceneActorNone;
}

void setFloat(void* user, float value) {
    *static_cast<float*>(user) = value;
}

void onPose(void* user, float value) {
    const auto* field = static_cast<const InspectorField*>(user);
    const uint8_t a = selectedActor(*field->engine);
    ActorPose pose{};
    if (a != kSceneActorNone && sceneFrameActorPose(field->engine->scene, a, pose)) {
        *poseSlot(pose, field->index) = value;
        sceneFrameActorSetPose(field->engine->scene, a, pose);
    }
}

void pushSwatch(Engine& e, const ActorPbr& pbr) {
    char swatch[96];
    std::snprintf(swatch, sizeof(swatch), "%smatSec::body::preview", kRoot);
    guiHostSetColor(e.gui, swatch, pbr.base[0], pbr.base[1], pbr.base[2]);
}

void onPbr(void* user, float value) {
    const auto* field = static_cast<const InspectorField*>(user);
    Engine& e = *field->engine;
    const uint8_t a = selectedActor(e);
    ActorPbr pbr{};
    if (a != kSceneActorNone && sceneFrameActorPbr(e.scene, a, pbr)) {
        *pbrSlot(pbr, field->index) = value;
        sceneFrameActorSetPbr(e.scene, a, pbr);
        pushSwatch(e, pbr);
    }
}

void onClick(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    const Layout layout = layoutOf(e.editor);
    for (const Toggle& t : layout.toggle) {
        if (t.action == action) {
            if (t.word != nullptr) {
                *t.word = *t.word != 0 ? 0 : 1;
            } else {
                *t.byte = *t.byte != 0 ? 0 : 1;
            }
            return;
        }
    }
    const uint8_t a = selectedActor(e);
    ActorPose pose{};
    if (a == kSceneActorNone || !sceneFrameActorPose(e.scene, a, pose)) {
        return;
    }
    switch (action) {
    case kActStatic: pose.isStatic = pose.isStatic != 0 ? 0 : 1; break;
    case kActResetPos: pose.pos[0] = pose.pos[1] = pose.pos[2] = 0.0f; break;
    case kActResetRot: pose.euler[0] = pose.euler[1] = pose.euler[2] = 0.0f; break;
    case kActResetScale: pose.scale[0] = pose.scale[1] = pose.scale[2] = 1.0f; break;
    default: return;
    }
    sceneFrameActorSetPose(e.scene, a, pose);
}

void showToggle(Engine& e, const char* section, const char* row, uint8_t& last, bool on) {
    if (last == static_cast<uint8_t>(on)) {
        return;
    }
    last = static_cast<uint8_t>(on);
    char node[96];
    std::snprintf(node, sizeof(node), "%s%sSec::body::%s::t", kRoot, section, row);
    guiHostSetText(e.gui, node, on ? "On" : "Off");
    guiHostSetActive(e.gui, node, on);
}

void followSelection(Engine& e, uint8_t actor) {
    char name[24] = "No selection";
    uint8_t kind = 0;
    if (actor != kSceneActorNone) {
        sceneFrameActorName(e.scene, actor, name, sizeof(name), &kind);
    }
    const uint8_t sections = actor != kSceneActorNone ? sceneFrameActorSections(e.scene, actor) : 0;
    char node[96];
    std::snprintf(node, sizeof(node), "%stitle::actor", kRoot);
    guiHostSetText(e.gui, node, name);
    std::snprintf(node, sizeof(node), "%sxformSec", kRoot);
    guiHostSetShown(e.gui, node, (sections & kSecTransform) != 0);
    std::snprintf(node, sizeof(node), "%smatSec", kRoot);
    guiHostSetShown(e.gui, node, (sections & kSecMaterial) != 0);
    ActorPbr pbr{};
    if ((sections & kSecMaterial) != 0 && sceneFrameActorPbr(e.scene, actor, pbr)) {
        for (uint32_t i = 0; i < 6; ++i) {
            path(node, sizeof(node), "mat", kPbrLeaf[i]);
            guiHostSetValue(e.gui, node, *pbrSlot(pbr, i));
        }
        pushSwatch(e, pbr);
    }
}

} // namespace

bool inspectorBind(Engine& e) {
    InspectorState& st = e.inspector;
    bool ok = true;
    char node[96];
    Layout layout = layoutOf(e.editor);
    for (const Slot& s : layout.slot) {
        path(node, sizeof(node), s.section, s.leaf);
        ok = guiHostBindDrag(e.gui, node, setFloat, s.value) && ok;
        guiHostSetValue(e.gui, node, *s.value);
    }
    for (uint32_t i = 0; i < 9; ++i) {
        st.pose[i] = {&e, i};
        posePath(node, sizeof(node), i);
        ok = guiHostBindDrag(e.gui, node, onPose, &st.pose[i]) && ok;
    }
    for (uint32_t i = 0; i < 6; ++i) {
        st.pbr[i] = {&e, i};
        path(node, sizeof(node), "mat", kPbrLeaf[i]);
        ok = guiHostBindDrag(e.gui, node, onPbr, &st.pbr[i]) && ok;
    }
    for (const Toggle& t : layout.toggle) {
        ok = guiHostBindClick(e.gui, t.action, onClick, &e) && ok;
    }
    for (uint32_t action : {kActStatic, kActResetPos, kActResetRot, kActResetScale}) {
        ok = guiHostBindClick(e.gui, action, onClick, &e) && ok;
    }
    for (uint8_t& last : st.lastToggle) {
        last = 0xff;
    }
    return ok;
}

void inspectorSync(Engine& e) {
    InspectorState& st = e.inspector;
    const uint8_t actor = selectedActor(e);
    if (actor != st.shown) {
        st.shown = actor;
        followSelection(e, actor);
    }
    ActorPose pose{};
    if (actor != kSceneActorNone && sceneFrameActorPose(e.scene, actor, pose)) {
        char node[96];
        for (uint32_t i = 0; i < 9; ++i) {
            posePath(node, sizeof(node), i);
            guiHostSetValue(e.gui, node, *poseSlot(pose, i));
        }
        showToggle(e, "xform", "static", st.lastToggle[15], pose.isStatic != 0);
    }
    const Layout layout = layoutOf(e.editor);
    uint32_t n = 0;
    for (const Toggle& t : layout.toggle) {
        showToggle(e, t.section, t.row, st.lastToggle[n++], t.word != nullptr ? *t.word != 0 : *t.byte != 0);
    }
}

} // namespace burnhope
