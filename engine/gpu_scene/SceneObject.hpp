#pragma once

#include "gpu_scene/Camera.hpp"

#include <flecs.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace burnhope {

constexpr uint8_t kSceneActorNone = 0xff;

enum class SceneActorKind : uint8_t { Sun = 1, Post = 2, Prop = 3 };

// Одна поза на всех. Солнце и пост её подключают, свою копию не заводят.
struct Xform {
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
};

// Поза актора. Гизмо пишет Xform, инспектор пишет тот же набор через Transform.
struct alignas(16) Transform {
    float pos[3]{};
    float pad0 = 0.0f;
    float euler[3]{};
    float pad1 = 0.0f;
    float scale[3]{1.0f, 1.0f, 1.0f};
    uint8_t isStatic = 0;
    uint8_t pad2[3]{};
};

// Режимы пост-обработки. Секция в инспекторе появляется, пока компонент висит на акторе.
struct alignas(16) PostProcess {
    uint8_t direct = 1;
    uint8_t indirect = 1;
    uint8_t reflect = 2;
    uint8_t ao = 1;
    uint8_t sscs = 1;
    uint8_t bloom = 1;
    uint8_t aa = 3;
    uint8_t tone = 0;
    uint8_t pad[8]{};
};

// Туман и облака. Отдельный модуль, не часть пост-обработки.
struct alignas(16) Atmosphere {
    float density = 0.06f;
    float falloff = 0.08f;
    float height = 0.0f;
    float start = 0.0f;
    float opacity = 0.92f;
    float luminance = 1.4f;
    float shafts = 40.0f;
    float shaftStart = 0.0f;
    float color[3]{0.62f, 0.72f, 0.86f};
    uint8_t enabled = 1;
    uint8_t pad[3]{};
};

// PBR куба. Текстуры остаются в MaterialGpuData, здесь ползунки инспектора.
struct alignas(16) Material {
    float base[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissive = 0.0f;
    float reflectance = 0.5f;
};

struct ActorKind {
    uint8_t v = 0;
    uint8_t slot = 0;
};

struct ActorName {
    char text[24]{};
};

struct SunObj {};
struct PostObj {};
struct PropObj {};

static_assert(std::is_trivially_copyable_v<Xform>);
static_assert(std::is_trivially_copyable_v<Transform>);
static_assert(std::is_trivially_copyable_v<Material>);
static_assert(sizeof(Transform) == 48);
static_assert(sizeof(Material) == 32);
static_assert(sizeof(PostProcess) == 16);
static_assert(sizeof(Atmosphere) == 48);
static_assert(std::is_trivially_copyable_v<PostProcess>);
static_assert(std::is_trivially_copyable_v<Atmosphere>);
static_assert(std::is_trivially_copyable_v<ActorKind>);
static_assert(std::is_trivially_copyable_v<ActorName>);

struct GizmoGpu {
    float viewProj[16]{};
    float origin[4]{};
    float ax[4]{};
    float ay[4]{};
    float az[4]{};
    float cam[4]{};
    float misc[4]{};
    float marker[8][4]{};
    float padAlign[8]{};
};

static_assert(sizeof(GizmoGpu) == 320, "gizmo uniform");
static_assert(sizeof(GizmoGpu) % 64 == 0, "uniform flight align");

struct ActorView {
    Xform x{};
    uint8_t kind = 0;
    uint8_t used = 0;
    char name[24]{};
};

struct SceneActors {
    flecs::world world{};
    flecs::query<Xform, ActorKind, ActorName> list;
    std::vector<flecs::entity> ent;
    uint8_t count = 0;
    uint8_t selected = kSceneActorNone;
    uint8_t gizmo = 0;
    uint8_t drag = kSceneActorNone;
    uint8_t grabScreen = 0;
    uint8_t camReady = 0;
    float grabAlong = 1.0f;
    float grabAngle = 0.0f;
    float grabEuler[3]{};
    float grabScale[3]{1.0f, 1.0f, 1.0f};
    float grabPos[3]{};
    Mat4 viewProj{};
    float eye[3]{};
    float right[3]{1.0f, 0.0f, 0.0f};
    float up[3]{0.0f, 1.0f, 0.0f};
    float forward[3]{0.0f, 0.0f, -1.0f};
    float tanX = 1.0f;
    float tanY = 1.0f;
    float extentW = 1.0f;
    float extentH = 1.0f;

    SceneActors() : list(world.query<Xform, ActorKind, ActorName>()) {}
    SceneActors(const SceneActors&) = delete;
    SceneActors& operator=(const SceneActors&) = delete;
};

inline void actorBasis(const float euler[3], float x[3], float y[3], float z[3]) {
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

inline void actorEulerFromBasis(const float x[3], const float y[3], const float z[3], float euler[3]) {
    euler[0] = std::asin(std::fmax(-1.0f, std::fmin(1.0f, -z[1]))) * 57.2957795f;
    if (std::fabs(z[1]) < 0.999f) {
        euler[1] = std::atan2(z[0], z[2]) * 57.2957795f;
        euler[2] = std::atan2(x[1], y[1]) * 57.2957795f;
    } else {
        euler[1] = std::atan2(-x[2], x[0]) * 57.2957795f;
        euler[2] = 0.0f;
    }
}

inline void actorSpin(const float grabEuler[3], int axis, float deltaDeg, float outEuler[3]) {
    float b[3][3];
    actorBasis(grabEuler, b[0], b[1], b[2]);
    const float ax[3] = {b[axis][0], b[axis][1], b[axis][2]};
    const float rad = deltaDeg * 0.017453292f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    for (int i = 0; i < 3; ++i) {
        if (i == axis) {
            continue;
        }
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

inline void actorCopyName(char* dst, const char* src) {
    uint8_t n = 0;
    if (src != nullptr) {
        while (src[n] != '\0' && n < 23) {
            dst[n] = src[n];
            ++n;
        }
    }
    dst[n] = '\0';
}

inline const Xform* actorXformOf(const SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<Xform>()) {
        return nullptr;
    }
    return &world.ent[index].get<Xform>();
}

inline Xform* actorXformMut(SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<Xform>()) {
        return nullptr;
    }
    return &world.ent[index].get_mut<Xform>();
}

inline uint8_t actorKindOf(const SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<ActorKind>()) {
        return 0;
    }
    return world.ent[index].get<ActorKind>().v;
}

inline const char* actorNameOf(const SceneActors& world, uint8_t index) {
    if (index >= world.count || !world.ent[index].is_alive() || !world.ent[index].has<ActorName>()) {
        return "";
    }
    return world.ent[index].get<ActorName>().text;
}

inline void actorStoreXform(SceneActors& world, uint8_t slot, const Xform& src) {
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

inline Xform sceneActorXform(const SceneActors& world, uint8_t index) {
    const Xform* x = actorXformOf(world, index);
    return x != nullptr ? *x : Xform{};
}

inline void sceneActorWriteXform(SceneActors& world, uint8_t index, const Xform& src) {
    actorStoreXform(world, index, src);
}

inline void sceneActorsClear(SceneActors& world) {
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

inline uint8_t sceneActorAdd(SceneActors& world, SceneActorKind kind, const float pos[3], const char* name) {
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
    if (kind == SceneActorKind::Sun) {
        ent.add<SunObj>();
    } else if (kind == SceneActorKind::Prop) {
        ent.add<PropObj>();
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

inline uint8_t sceneActorDuplicate(SceneActors& world) {
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

inline void actorSub(const float a[3], const float b[3], float o[3]) {
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

inline float actorDot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float actorLen(const float a[3]) {
    return std::sqrt(actorDot(a, a));
}

inline void actorCross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

inline void sceneActorSetCamera(SceneActors& world, const CameraCull& cam, float width, float height) {
    world.viewProj = cam.viewProj;
    world.eye[0] = cam.pos[0];
    world.eye[1] = cam.pos[1];
    world.eye[2] = cam.pos[2];
    world.right[0] = cam.right[0];
    world.right[1] = cam.right[1];
    world.right[2] = cam.right[2];
    world.up[0] = cam.up[0];
    world.up[1] = cam.up[1];
    world.up[2] = cam.up[2];
    world.forward[0] = cam.forward[0];
    world.forward[1] = cam.forward[1];
    world.forward[2] = cam.forward[2];
    world.tanX = cam.tanHalfFovX;
    world.tanY = cam.tanHalfFovY;
    world.extentW = width > 1.0f ? width : 1.0f;
    world.extentH = height > 1.0f ? height : 1.0f;
    world.camReady = 1;
}

inline void actorRay(const SceneActors& world, float px, float py, float ro[3], float rd[3]) {
    ro[0] = world.eye[0];
    ro[1] = world.eye[1];
    ro[2] = world.eye[2];
    const float ndcX = (px / world.extentW) * 2.0f - 1.0f;
    const float ndcY = (py / world.extentH) * 2.0f - 1.0f;
    const float vx = ndcX * world.tanX;
    const float vy = -ndcY * world.tanY;
    rd[0] = world.right[0] * vx + world.up[0] * vy + world.forward[0];
    rd[1] = world.right[1] * vx + world.up[1] * vy + world.forward[1];
    rd[2] = world.right[2] * vx + world.up[2] * vy + world.forward[2];
    const float n = actorLen(rd);
    if (n > 1.0e-6f) {
        rd[0] /= n;
        rd[1] /= n;
        rd[2] /= n;
    }
}

inline bool actorProject(const SceneActors& world, const float p[3], float& sx, float& sy) {
    const Mat4& m = world.viewProj;
    const float cx = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2] + m.m[12];
    const float cy = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2] + m.m[13];
    const float cw = m.m[3] * p[0] + m.m[7] * p[1] + m.m[11] * p[2] + m.m[15];
    if (cw <= 1.0e-4f) {
        return false;
    }
    sx = (cx / cw * 0.5f + 0.5f) * world.extentW;
    sy = (cy / cw * 0.5f + 0.5f) * world.extentH;
    return true;
}

inline float actorScreenDist(float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float ab2 = abx * abx + aby * aby;
    float t = ab2 > 1.0e-4f ? ((px - ax) * abx + (py - ay) * aby) / ab2 : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    const float dx = px - (ax + abx * t);
    const float dy = py - (ay + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

inline float actorScreenAlong(float px, float py, float ax, float ay, float bx, float by, bool& ok) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float ab2 = abx * abx + aby * aby;
    ok = ab2 >= 4.0f;
    if (!ok) {
        return 0.0f;
    }
    return ((px - ax) * abx + (py - ay) * aby) / ab2;
}

inline bool actorRayPlane(const float ro[3], const float rd[3], const float p0[3], const float n[3], float& t) {
    const float denom = actorDot(rd, n);
    if (std::fabs(denom) < 1.0e-6f) {
        return false;
    }
    float w[3];
    actorSub(p0, ro, w);
    t = actorDot(w, n) / denom;
    return t > 0.0f;
}

inline float actorRayAxis(const float ro[3], const float rd[3], const float p0[3], const float dir[3], float& along) {
    float w0[3];
    actorSub(ro, p0, w0);
    const float b = actorDot(rd, dir);
    const float d = actorDot(rd, w0);
    const float e = actorDot(dir, w0);
    const float denom = 1.0f - b * b;
    if (denom < 1.0e-6f) {
        along = e;
    } else {
        along = (e - b * d) / denom;
    }
    const float hit[3] = {p0[0] + dir[0] * along, p0[1] + dir[1] * along, p0[2] + dir[2] * along};
    float closest[3];
    float t = actorDot(w0, rd);
    if (denom >= 1.0e-6f) {
        t = (b * e - d) / denom;
    }
    closest[0] = ro[0] + rd[0] * t;
    closest[1] = ro[1] + rd[1] * t;
    closest[2] = ro[2] + rd[2] * t;
    float delta[3];
    actorSub(closest, hit, delta);
    return actorLen(delta);
}

inline float actorSpan(const SceneActors& world, const float pos[3]) {
    float d[3];
    actorSub(pos, world.eye, d);
    float len = actorLen(d) * 0.18f;
    if (len < 0.75f) {
        len = 0.75f;
    }
    if (len > 12.0f) {
        len = 12.0f;
    }
    return len;
}

inline float actorPickPx(const SceneActors& world, const float origin[3], float span) {
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
    const float side[3] = {
        origin[0] + world.right[0] * span * 0.04f,
        origin[1] + world.right[1] * span * 0.04f,
        origin[2] + world.right[2] * span * 0.04f,
    };
    if (!actorProject(world, origin, ax, ay) || !actorProject(world, side, bx, by)) {
        return 10.0f;
    }
    float px = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) * 2.2f;
    if (px < 8.0f) {
        px = 8.0f;
    }
    if (px > 16.0f) {
        px = 16.0f;
    }
    return px;
}

inline bool actorAlong(const SceneActors& world, float px, float py, const float origin[3], const float dir[3], float& along, uint8_t& screen) {
    float ro[3];
    float rd[3];
    actorRay(world, px, py, ro, rd);
    const float align = std::fabs(actorDot(rd, dir));
    if (align > 0.92f) {
        float ax = 0.0f;
        float ay = 0.0f;
        float bx = 0.0f;
        float by = 0.0f;
        const float tip[3] = {origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2]};
        bool ok = false;
        if (actorProject(world, origin, ax, ay) && actorProject(world, tip, bx, by)) {
            along = actorScreenAlong(px, py, ax, ay, bx, by, ok);
        }
        if (ok) {
            screen = 1;
            return true;
        }
    }
    actorRayAxis(ro, rd, origin, dir, along);
    screen = 0;
    return true;
}

inline float actorRingAngle(const SceneActors& world, float px, float py, const float origin[3], const float axis[3], uint8_t& screen) {
    float ox = 0.0f;
    float oy = 0.0f;
    float ro[3];
    float rd[3];
    actorRay(world, px, py, ro, rd);
    const bool flat = std::fabs(actorDot(rd, axis)) <= 0.2f;
    if (screen == 0xff) {
        screen = flat ? 1 : 0;
    }
    if (screen == 0 && !flat) {
        float t = 0.0f;
        if (actorRayPlane(ro, rd, origin, axis, t)) {
            const float hit[3] = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
            float ref[3] = {0.0f, 1.0f, 0.0f};
            if (std::fabs(axis[1]) >= 0.9f) {
                ref[0] = 1.0f;
                ref[1] = 0.0f;
            }
            float u[3];
            actorCross(axis, ref, u);
            const float un = actorLen(u);
            if (un > 1.0e-4f) {
                u[0] /= un;
                u[1] /= un;
                u[2] /= un;
                float v[3];
                actorCross(axis, u, v);
                float rel[3];
                actorSub(hit, origin, rel);
                return std::atan2(actorDot(rel, v), actorDot(rel, u)) * 57.2957795f;
            }
        }
    }
    screen = 1;
    if (!actorProject(world, origin, ox, oy)) {
        return 0.0f;
    }
    return std::atan2(py - oy, px - ox) * 57.2957795f;
}

inline void sceneActorFillGpu(const SceneActors& world, GizmoGpu& gpu) {
    std::memcpy(gpu.viewProj, world.viewProj.m, sizeof(gpu.viewProj));
    std::memcpy(gpu.cam, world.eye, sizeof(float) * 3);
    gpu.cam[3] = 1.0f;
    gpu.origin[3] = 0.0f;
    gpu.misc[1] = static_cast<float>(world.gizmo);
    gpu.misc[2] = world.drag < 3 ? static_cast<float>(world.drag) : 3.0f;
    uint8_t markers = 0;
    for (uint8_t i = 0; i < world.count && markers < 8; ++i) {
        const Xform* x = actorXformOf(world, i);
        if (x == nullptr) {
            continue;
        }
        gpu.marker[markers][0] = x->pos[0];
        gpu.marker[markers][1] = x->pos[1];
        gpu.marker[markers][2] = x->pos[2];
        gpu.marker[markers][3] = static_cast<float>(actorKindOf(world, i));
        markers += 1;
    }
    gpu.misc[3] = static_cast<float>(markers);
    const Xform* selected = actorXformOf(world, world.selected);
    if (selected == nullptr) {
        gpu.misc[0] = 1.0f;
        return;
    }
    const Xform& xform = *selected;
    gpu.origin[0] = xform.pos[0];
    gpu.origin[1] = xform.pos[1];
    gpu.origin[2] = xform.pos[2];
    gpu.origin[3] = 1.0f;
    float x[3];
    float y[3];
    float z[3];
    actorBasis(xform.euler, x, y, z);
    std::memcpy(gpu.ax, x, sizeof(x));
    std::memcpy(gpu.ay, y, sizeof(y));
    std::memcpy(gpu.az, z, sizeof(z));
    gpu.misc[0] = actorSpan(world, xform.pos);
}

inline uint8_t sceneActorHit(const SceneActors& world, float px, float py, uint8_t& axis) {
    axis = kSceneActorNone;
    if (world.camReady == 0 || world.count == 0) {
        return kSceneActorNone;
    }
    const Xform* selectedX = actorXformOf(world, world.selected);
    if (selectedX != nullptr) {
        const Xform& xform = *selectedX;
        float basis[3][3];
        actorBasis(xform.euler, basis[0], basis[1], basis[2]);
        const float span = actorSpan(world, xform.pos);
        const float limit = actorPickPx(world, xform.pos, span);
        float best = limit;
        float ox = 0.0f;
        float oy = 0.0f;
        if (!actorProject(world, xform.pos, ox, oy)) {
            return kSceneActorNone;
        }
        for (uint8_t a = 0; a < 3; ++a) {
            if (world.gizmo == 1) {
                float u[3];
                float ref[3] = {0.0f, 1.0f, 0.0f};
                if (std::fabs(basis[a][1]) >= 0.9f) {
                    ref[0] = 1.0f;
                    ref[1] = 0.0f;
                }
                actorCross(basis[a], ref, u);
                const float un = actorLen(u);
                if (un < 1.0e-4f) {
                    continue;
                }
                u[0] /= un;
                u[1] /= un;
                u[2] /= un;
                float v[3];
                actorCross(basis[a], u, v);
                float ring = limit;
                float prevX = 0.0f;
                float prevY = 0.0f;
                bool prev = false;
                for (int s = 0; s <= 32; ++s) {
                    const float ang = static_cast<float>(s) * 0.19634954f;
                    const float point[3] = {
                        xform.pos[0] + (u[0] * std::cos(ang) + v[0] * std::sin(ang)) * span,
                        xform.pos[1] + (u[1] * std::cos(ang) + v[1] * std::sin(ang)) * span,
                        xform.pos[2] + (u[2] * std::cos(ang) + v[2] * std::sin(ang)) * span,
                    };
                    float sx = 0.0f;
                    float sy = 0.0f;
                    if (!actorProject(world, point, sx, sy)) {
                        prev = false;
                        continue;
                    }
                    if (prev) {
                        const float d = actorScreenDist(px, py, prevX, prevY, sx, sy);
                        if (d < ring) {
                            ring = d;
                        }
                    }
                    prevX = sx;
                    prevY = sy;
                    prev = true;
                }
                if (ring < best) {
                    best = ring;
                    axis = a;
                }
                continue;
            }
            const float lo = world.gizmo == 2 ? 0.62f : 0.18f;
            const float hi = 1.12f;
            const float a0[3] = {
                xform.pos[0] + basis[a][0] * span * lo,
                xform.pos[1] + basis[a][1] * span * lo,
                xform.pos[2] + basis[a][2] * span * lo,
            };
            const float a1[3] = {
                xform.pos[0] + basis[a][0] * span * hi,
                xform.pos[1] + basis[a][1] * span * hi,
                xform.pos[2] + basis[a][2] * span * hi,
            };
            float sx = 0.0f;
            float sy = 0.0f;
            float tx = 0.0f;
            float ty = 0.0f;
            if (!actorProject(world, a0, sx, sy) || !actorProject(world, a1, tx, ty)) {
                continue;
            }
            const float dist = actorScreenDist(px, py, sx, sy, tx, ty);
            if (dist < best) {
                best = dist;
                axis = a;
            }
        }
        if (axis != kSceneActorNone) {
            return world.selected;
        }
    }
    uint8_t found = kSceneActorNone;
    float best = 1.0e9f;
    for (uint8_t i = 0; i < world.count; ++i) {
        const Xform* actor = actorXformOf(world, i);
        if (actor == nullptr) {
            continue;
        }
        const float span = actorSpan(world, actor->pos);
        float sx = 0.0f;
        float sy = 0.0f;
        float tx = 0.0f;
        float ty = 0.0f;
        const float arm[3] = {
            actor->pos[0] + world.right[0] * span * 0.32f,
            actor->pos[1] + world.right[1] * span * 0.32f,
            actor->pos[2] + world.right[2] * span * 0.32f,
        };
        if (!actorProject(world, actor->pos, sx, sy) || !actorProject(world, arm, tx, ty)) {
            continue;
        }
        float radius = std::sqrt((tx - sx) * (tx - sx) + (ty - sy) * (ty - sy));
        if (radius < 10.0f) {
            radius = 10.0f;
        }
        if (radius > 28.0f) {
            radius = 28.0f;
        }
        const float dist = std::sqrt((px - sx) * (px - sx) + (py - sy) * (py - sy));
        if (dist < radius && dist < best) {
            best = dist;
            found = i;
        }
    }
    return found;
}

inline void sceneActorPointer(SceneActors& world, float x, float y, uint8_t down, uint8_t pressed, uint8_t released) {
    if (world.camReady == 0) {
        return;
    }
    if (released != 0) {
        world.drag = kSceneActorNone;
    }
    if (pressed != 0) {
        uint8_t axis = kSceneActorNone;
        const uint8_t hit = sceneActorHit(world, x, y, axis);
        const Xform* pressedX = actorXformOf(world, world.selected);
        if (axis < 3 && hit == world.selected && pressedX != nullptr) {
            world.drag = axis;
            const Xform& xform = *pressedX;
            world.grabPos[0] = xform.pos[0];
            world.grabPos[1] = xform.pos[1];
            world.grabPos[2] = xform.pos[2];
            world.grabEuler[0] = xform.euler[0];
            world.grabEuler[1] = xform.euler[1];
            world.grabEuler[2] = xform.euler[2];
            world.grabScale[0] = xform.scale[0];
            world.grabScale[1] = xform.scale[1];
            world.grabScale[2] = xform.scale[2];
            float basis[3][3];
            actorBasis(xform.euler, basis[0], basis[1], basis[2]);
            world.grabScreen = 0;
            actorAlong(world, x, y, xform.pos, basis[axis], world.grabAlong, world.grabScreen);
            if (std::fabs(world.grabAlong) < 0.05f) {
                world.grabAlong = world.grabAlong < 0.0f ? -0.05f : 0.05f;
            }
            if (world.gizmo == 1) {
                uint8_t mode = 0xff;
                world.grabAngle = actorRingAngle(world, x, y, xform.pos, basis[axis], mode);
                world.grabScreen = mode;
            }
        } else if (hit != kSceneActorNone) {
            world.selected = hit;
            world.drag = kSceneActorNone;
        }
    }
    Xform* live = actorXformMut(world, world.selected);
    if (world.drag > 2 || live == nullptr || down == 0) {
        return;
    }
    Xform& xform = *live;
    float basis[3][3];
    actorBasis(world.grabEuler, basis[0], basis[1], basis[2]);
    const uint8_t axis = world.drag;
    if (world.gizmo == 1) {
        uint8_t mode = world.grabScreen;
        const float ang = actorRingAngle(world, x, y, world.grabPos, basis[axis], mode);
        float delta = ang - world.grabAngle;
        if (delta > 180.0f) {
            delta -= 360.0f;
        }
        if (delta < -180.0f) {
            delta += 360.0f;
        }
        actorSpin(world.grabEuler, axis, delta, xform.euler);
        return;
    }
    float along = 0.0f;
    uint8_t screen = 0;
    actorAlong(world, x, y, world.grabPos, basis[axis], along, screen);
    if (screen != world.grabScreen) {
        return;
    }
    if (world.gizmo == 2) {
        const float next = world.grabScale[axis] * (along / world.grabAlong);
        xform.scale[axis] = next < 0.05f ? 0.05f : next;
        return;
    }
    const float delta = along - world.grabAlong;
    xform.pos[0] = world.grabPos[0] + basis[axis][0] * delta;
    xform.pos[1] = world.grabPos[1] + basis[axis][1] * delta;
    xform.pos[2] = world.grabPos[2] + basis[axis][2] * delta;
}

} // namespace burnhope
