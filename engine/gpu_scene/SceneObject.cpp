#include "gpu_scene/SceneObject.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace burnhope {

void actorBasis(const float euler[3], float x[3], float y[3], float z[3]) {
    const float yaw = euler[1] * 0.017453292f;
    const float pitch = euler[0] * 0.017453292f;
    const float roll = euler[2] * 0.017453292f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cr = std::cos(roll);
    const float sr = std::sin(roll);
    x[0] = cy * cr + sy * sp * sr;
    x[1] = cp * sr;
    x[2] = -sy * cr + cy * sp * sr;
    y[0] = -cy * sr + sy * sp * cr;
    y[1] = cp * cr;
    y[2] = sy * sr + cy * sp * cr;
    z[0] = sy * cp;
    z[1] = -sp;
    z[2] = cy * cp;
}

void actorEulerFromBasis(const float x[3], const float y[3], const float z[3], float euler[3]) {
    euler[0] = std::asin(std::fmax(-1.0f, std::fmin(1.0f, -z[1]))) * 57.2957795f;
    if (std::fabs(z[1]) < 0.999f) {
        euler[1] = std::atan2(z[0], z[2]) * 57.2957795f;
        euler[2] = std::atan2(x[1], y[1]) * 57.2957795f;
    } else {
        euler[1] = std::atan2(-x[2], x[0]) * 57.2957795f;
        euler[2] = 0.0f;
    }
}

void actorSpinAround(const float grabEuler[3], const float axis[3], float deltaDeg, float outEuler[3]) {
    float b[3][3];
    actorBasis(grabEuler, b[0], b[1], b[2]);
    float ax[3] = {axis[0], axis[1], axis[2]};
    const float al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
    if (al < 1.0e-6f) {
        outEuler[0] = grabEuler[0];
        outEuler[1] = grabEuler[1];
        outEuler[2] = grabEuler[2];
        return;
    }
    ax[0] /= al;
    ax[1] /= al;
    ax[2] /= al;
    const float rad = deltaDeg * 0.017453292f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    for (int i = 0; i < 3; ++i) {
        const float v0 = b[i][0];
        const float v1 = b[i][1];
        const float v2 = b[i][2];
        const float cx = ax[1] * v2 - ax[2] * v1;
        const float cy = ax[2] * v0 - ax[0] * v2;
        const float cz = ax[0] * v1 - ax[1] * v0;
        const float d = ax[0] * v0 + ax[1] * v1 + ax[2] * v2;
        b[i][0] = v0 * c + cx * s + ax[0] * d * (1.0f - c);
        b[i][1] = v1 * c + cy * s + ax[1] * d * (1.0f - c);
        b[i][2] = v2 * c + cz * s + ax[2] * d * (1.0f - c);
    }
    actorEulerFromBasis(b[0], b[1], b[2], outEuler);
}

void actorSpin(const float grabEuler[3], int axis, float deltaDeg, float outEuler[3]) {
    float b[3][3];
    actorBasis(grabEuler, b[0], b[1], b[2]);
    actorSpinAround(grabEuler, b[axis], deltaDeg, outEuler);
}

static void actorCopyName(char* dst, const char* src) {
    uint8_t n = 0;
    if (src != nullptr) {
        while (src[n] != '\0' && n < 23) {
            dst[n] = src[n];
            ++n;
        }
    }
    dst[n] = '\0';
}

const Xform* actorXformOf(const SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<Xform>()) {
        return nullptr;
    }
    return &world.ent[index].get<Xform>();
}

Xform* actorXformMut(SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<Xform>()) {
        return nullptr;
    }
    return &world.ent[index].get_mut<Xform>();
}

uint8_t actorKindOf(const SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<ActorKind>()) {
        return 0;
    }
    return world.ent[index].get<ActorKind>().v;
}

const char* actorNameOf(const SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<ActorName>()) {
        return "";
    }
    return world.ent[index].get<ActorName>().text;
}

static void actorStoreXform(SceneActors& world, uint8_t slot, const Xform& src) {
    if (Xform* x = actorXformMut(world, slot)) {
        *x = src;
    }
    if (slot >= world.count || !world.ent[slot].is_alive() || !world.ent[slot].has<Transform>()) {
        return;
    }
    Transform& pose = world.ent[slot].get_mut<Transform>();
    pose.pos[0] = src.pos[0];
    pose.pos[1] = src.pos[1];
    pose.pos[2] = src.pos[2];
    pose.euler[0] = src.euler[0];
    pose.euler[1] = src.euler[1];
    pose.euler[2] = src.euler[2];
    pose.scale[0] = src.scale[0];
    pose.scale[1] = src.scale[1];
    pose.scale[2] = src.scale[2];
}

Xform sceneActorXform(const SceneActors& world, uint8_t index) {
    const Xform* x = actorXformOf(world, index);
    return x != nullptr ? *x : Xform{};
}

void sceneActorWriteXform(SceneActors& world, uint8_t index, const Xform& src) {
    actorStoreXform(world, index, src);
}

void sceneActorsClear(SceneActors& world) {
    for (flecs::entity& ent : world.ent) {
        if (ent.is_alive()) {
            ent.destruct();
        }
    }
    world.ent.clear();
    world.count = 0;
    world.selected = kSceneActorNone;
    world.drag = kSceneActorNone;
}

uint8_t sceneActorAdd(SceneActors& world, SceneActorKind kind, const float pos[3], const char* name) {
    if (world.ent.size() >= static_cast<size_t>(kSceneActorNone)) {
        return kSceneActorNone;
    }
    const uint8_t index = static_cast<uint8_t>(world.ent.size());
    Xform xform{};
    xform.scale[0] = xform.scale[1] = xform.scale[2] = 1.0f;
    if (pos != nullptr) {
        xform.pos[0] = pos[0];
        xform.pos[1] = pos[1];
        xform.pos[2] = pos[2];
    }
    ActorKind tag{};
    tag.v = static_cast<uint8_t>(kind);
    tag.slot = index;
    ActorName label{};
    actorCopyName(label.text, name);
    flecs::entity ent = world.world.entity();
    ent.set<Xform>(xform);
    Transform pose{};
    pose.pos[0] = xform.pos[0];
    pose.pos[1] = xform.pos[1];
    pose.pos[2] = xform.pos[2];
    pose.euler[0] = xform.euler[0];
    pose.euler[1] = xform.euler[1];
    pose.euler[2] = xform.euler[2];
    pose.scale[0] = xform.scale[0];
    pose.scale[1] = xform.scale[1];
    pose.scale[2] = xform.scale[2];
    ent.set<Transform>(pose);
    if (kind == SceneActorKind::Prop) {
        Material mat{};
        mat.base[0] = mat.base[1] = mat.base[2] = mat.base[3] = 1.0f;
        mat.roughness = 0.5f;
        ent.set<Material>(mat);
    }
    ent.set<ActorKind>(tag);
    ent.set<ActorName>(label);
    ent.set<Pivot>({});
    if (kind == SceneActorKind::Sun) {
        ent.add<SunObj>();
    } else if (kind == SceneActorKind::Prop) {
        ent.set<ActorBounds>({});
        ent.add<PropObj>();
    } else if (kind == SceneActorKind::Weather) {
        ent.add<WeatherObj>();
    } else if (kind == SceneActorKind::Empty) {
        ent.add<EmptyObj>();
    } else if (kind == SceneActorKind::Light) {
        ent.add<LightObj>();
    } else {
        ent.set<PostProcess>({});
        ent.set<Atmosphere>({});
        ent.add<PostObj>();
    }
    world.ent.push_back(ent);
    world.count = static_cast<uint8_t>(world.ent.size());
    world.selected = index;
    return index;
}

uint8_t sceneActorDuplicate(SceneActors& world) {
    if (world.selected >= world.count) {
        return kSceneActorNone;
    }
    const Xform* srcPtr = actorXformOf(world, world.selected);
    if (srcPtr == nullptr) {
        return kSceneActorNone;
    }
    const Xform src = *srcPtr;
    const uint8_t kind = actorKindOf(world, world.selected);
    float pos[3] = {src.pos[0] + 2.0f, src.pos[1], src.pos[2]};
    char name[24];
    actorCopyName(name, actorNameOf(world, world.selected));
    const uint8_t n = static_cast<uint8_t>(std::strlen(name));
    if (n + 2 < 23) {
        name[n] = ' ';
        name[n + 1] = static_cast<char>('2' + (world.count > 9 ? 9 : world.count));
        name[n + 2] = '\0';
    }
    const uint8_t index = sceneActorAdd(world, static_cast<SceneActorKind>(kind), pos, name);
    if (index == kSceneActorNone) {
        return index;
    }
    Xform copy = src;
    copy.pos[0] = pos[0];
    copy.pos[1] = pos[1];
    copy.pos[2] = pos[2];
    actorStoreXform(world, index, copy);
    return index;
}

} // namespace burnhope
