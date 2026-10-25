#pragma once

#include "gpu_scene/Camera.hpp"

#include "gfx/Material.hpp"

#include <flecs.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace burnhope {

constexpr uint8_t kSceneActorNone = 0xff;

enum class SceneActorKind : uint8_t { Sun = 1, Post = 2, Prop = 3, Weather = 4, Empty = 5, Light = 6 };

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
    uint8_t aa = 4;
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
struct WeatherObj {};
struct EmptyObj {};
struct LightObj {};

// Смещение опорной точки в локальных осях актора, до поворота. Центр AABB — нулевой вектор.
struct alignas(16) Pivot {
    float local[3]{};
    float pad = 0.0f;
};

// Точечный или направленный источник, привязанный к актору. Индекс — слот в списке ламп кадра.
struct LightLink {
    uint32_t index = 0;
    uint32_t type = 1;
};

// Какой слот MaterialGpuData правит этот актор. Не пул физики: слотов пропов 16, материалов больше.
// anchor = позиция актёра минус перенос инстанса. Сдвиг гизмо прибавляется к матрице, меш не прыгает.
struct MeshBind {
    uint32_t material = 0;
    uint32_t instance = 0;
    float anchor[3]{};
    float pad = 0.0f;
};

struct ActorMat {
    BhmatDoc doc{};
};

// Инстансы одной модели. Сдвиг актора прибавляется ко всем, а не к одному мешлету.
struct MeshSpan {
    uint32_t begin = 0;
    uint32_t count = 1;
};

// Полурёбра меша в локальных осях, до scale. Обводка и клик читают только это.
struct alignas(16) ActorBounds {
    float half[3]{0.5f, 0.5f, 0.5f};
    float pad = 0.0f;
};

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
static_assert(std::is_trivially_copyable_v<ActorBounds>);
static_assert(std::is_trivially_copyable_v<MeshBind>);
static_assert(sizeof(MeshBind) == 24);
static_assert(std::is_trivially_copyable_v<ActorMat>);
static_assert(std::is_trivially_copyable_v<MeshSpan>);
static_assert(sizeof(MeshSpan) == 8);
static_assert(sizeof(ActorBounds) == 16);
static_assert(std::is_trivially_copyable_v<Pivot>);
static_assert(sizeof(Pivot) == 16);
static_assert(std::is_trivially_copyable_v<LightLink>);

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
    uint8_t tool = 0;
    uint8_t rotXY = 0;
    uint8_t drag = kSceneActorNone;
    uint8_t grabScreen = 0;
    uint8_t camReady = 0;
    uint8_t ortho = 0;
    float grabAlong = 1.0f;
    float grabAngle = 0.0f;
    float grabEuler[3]{};
    float grabScale[3]{1.0f, 1.0f, 1.0f};
    float grabPos[3]{};
    float grabOrg[3]{};
    float grabPivot[3]{};
    float grabPx = 0.0f;
    float grabPy = 0.0f;
    float grabHit[3]{};
    uint8_t space = 0;
    uint8_t pivotSpace = 0;
    uint8_t hot = kSceneActorNone;
    uint8_t mark = kSceneActorNone;
    uint8_t snapOn[3]{};
    float snapStep[3]{1.0f, 15.0f, 0.1f};
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

void actorBasis(const float euler[3], float x[3], float y[3], float z[3]);

void actorEulerFromBasis(const float x[3], const float y[3], const float z[3], float euler[3]);

void actorSpinAround(const float grabEuler[3], const float axis[3], float deltaDeg, float outEuler[3]);

void actorSpin(const float grabEuler[3], int axis, float deltaDeg, float outEuler[3]);

void actorCopyName(char* dst, const char* src);

const Xform* actorXformOf(const SceneActors& world, uint8_t index);

Xform* actorXformMut(SceneActors& world, uint8_t index);

uint8_t actorKindOf(const SceneActors& world, uint8_t index);

const char* actorNameOf(const SceneActors& world, uint8_t index);

void actorStoreXform(SceneActors& world, uint8_t slot, const Xform& src);

Xform sceneActorXform(const SceneActors& world, uint8_t index);

void sceneActorWriteXform(SceneActors& world, uint8_t index, const Xform& src);

void sceneActorsClear(SceneActors& world);

uint8_t sceneActorAdd(SceneActors& world, SceneActorKind kind, const float pos[3], const char* name);

uint8_t sceneActorDuplicate(SceneActors& world);

void actorSub(const float a[3], const float b[3], float o[3]);

float actorDot(const float a[3], const float b[3]);

float actorLen(const float a[3]);

void actorCross(const float a[3], const float b[3], float o[3]);

void sceneActorSetCamera(SceneActors& world, const CameraCull& cam, float width, float height);

void actorRay(const SceneActors& world, float px, float py, float ro[3], float rd[3]);

bool actorProject(const SceneActors& world, const float p[3], float& sx, float& sy);

float actorScreenDist(float px, float py, float ax, float ay, float bx, float by);

float actorScreenAlong(float px, float py, float ax, float ay, float bx, float by, bool& ok);

bool actorRayPlane(const float ro[3], const float rd[3], const float p0[3], const float n[3], float& t);

float actorRayAxis(const float ro[3], const float rd[3], const float p0[3], const float dir[3], float& along);

float actorMetersPerPx(const SceneActors& world, const float pos[3]);

// 72 px было базой значка. Вблизи ×4, вдали ×2, затем кламп в пикселях.
float actorIconPx(float dist);

// Экранные 108 px = прежние 72 × 1.5. Длина = distance * tan(fov/2) * (2*108/height).
float actorSpan(const SceneActors& world, const float pos[3]);

float actorSnap(float value, float step);

void gizmoBasis(const SceneActors& world, const float euler[3], float x[3], float y[3], float z[3]);

void actorPivotLocal(const SceneActors& world, uint8_t actor, float out[3]);

void actorWritePivot(SceneActors& world, uint8_t actor, const float local[3]);

void actorHandle(const SceneActors& world, const Xform& xform, uint8_t actor, float out[3]);

float actorPickPx(const SceneActors& world, const float origin[3], float span);

bool actorAlong(const SceneActors& world, float px, float py, const float origin[3], const float dir[3], float& along, uint8_t& screen, int stick = -1);

float actorRingAngle(const SceneActors& world, float px, float py, const float origin[3], const float axis[3], uint8_t& screen);

bool sceneActorSunRig(const SceneActors& world);

void sceneActorFitTool(SceneActors& world);

void sceneActorFillGpu(const SceneActors& world, GizmoGpu& gpu);

uint8_t sceneActorHit(const SceneActors& world, float px, float py, uint8_t& axis);

void sceneActorGrab(SceneActors& world, const Xform& xform);

void sceneActorScreenDelta(const SceneActors& world, float x, float y, float& alongRight, float& alongUp);

void sceneActorPointer(SceneActors& world, float x, float y, uint8_t down, uint8_t pressed, uint8_t released);

} // namespace burnhope
