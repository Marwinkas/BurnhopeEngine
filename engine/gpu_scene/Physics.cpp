#include "gpu_scene/Physics.hpp"

#include "gpu_scene/SceneObject.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>

namespace burnhope {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kStatic{0};
constexpr JPH::BroadPhaseLayer kMoving{1};
constexpr uint32_t kCount = 2;
} // namespace BroadPhaseLayers

constexpr uint32_t kDynCap = 16;

void joltTrace(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    spdlog::info("{}", line);
}

void joltBoot() {
    if (JPH::Factory::sInstance != nullptr) {
        return;
    }
    JPH::RegisterDefaultAllocator();
    JPH::Trace = joltTrace;
#if defined(JPH_ENABLE_ASSERTS)
    JPH::AssertFailed = [](const char* expr, const char* message, const char* file, uint32_t line) {
        spdlog::error("Jolt {} {}:{} {}", expr, file, line, message != nullptr ? message : "");
        return false;
    };
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

class BroadPhaseMap final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseMap() {
        map[Layers::kStatic] = BroadPhaseLayers::kStatic;
        map[Layers::kMoving] = BroadPhaseLayers::kMoving;
    }

    uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer < Layers::kCount ? map[layer] : BroadPhaseLayers::kStatic;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::kMoving ? "moving" : "static";
    }
#endif

private:
    JPH::BroadPhaseLayer map[Layers::kCount]{};
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        if (layer == Layers::kStatic) {
            return broad == BroadPhaseLayers::kMoving;
        }
        return true;
    }
};

class ObjectPair final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

void instancePoint(const GpuInstance& inst, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = inst.world[0] * x + inst.world[1] * y + inst.world[2] * z + inst.world[3];
    oy = inst.world[4] * x + inst.world[5] * y + inst.world[6] * z + inst.world[7];
    oz = inst.world[8] * x + inst.world[9] * y + inst.world[10] * z + inst.world[11];
}

void instanceAxes(const GpuInstance& inst, float scale[3], float x[3], float y[3], float z[3], float pos[3]) {
    x[0] = inst.world[0];
    x[1] = inst.world[4];
    x[2] = inst.world[8];
    y[0] = inst.world[1];
    y[1] = inst.world[5];
    y[2] = inst.world[9];
    z[0] = inst.world[2];
    z[1] = inst.world[6];
    z[2] = inst.world[10];
    scale[0] = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    scale[1] = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
    scale[2] = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    const float sx = scale[0] > 1.0e-4f ? scale[0] : 1.0f;
    const float sy = scale[1] > 1.0e-4f ? scale[1] : 1.0f;
    const float sz = scale[2] > 1.0e-4f ? scale[2] : 1.0f;
    x[0] /= sx;
    x[1] /= sx;
    x[2] /= sx;
    y[0] /= sy;
    y[1] /= sy;
    y[2] /= sy;
    z[0] /= sz;
    z[1] /= sz;
    z[2] /= sz;
    pos[0] = inst.world[3];
    pos[1] = inst.world[7];
    pos[2] = inst.world[11];
}

JPH::Quat quatFromEuler(const float euler[3]) {
    float x[3];
    float y[3];
    float z[3];
    actorBasis(euler, x, y, z);
    const JPH::Mat44 m{
        JPH::Vec4{x[0], x[1], x[2], 0.0f},
        JPH::Vec4{y[0], y[1], y[2], 0.0f},
        JPH::Vec4{z[0], z[1], z[2], 0.0f},
        JPH::Vec4{0.0f, 0.0f, 0.0f, 1.0f},
    };
    return m.GetQuaternion().Normalized();
}

void eulerFromQuat(JPH::Quat q, float euler[3]) {
    q = q.Normalized();
    const JPH::Mat44 m = JPH::Mat44::sRotation(q);
    const JPH::Vec3 x = m.GetColumn3(0);
    const JPH::Vec3 y = m.GetColumn3(1);
    const JPH::Vec3 z = m.GetColumn3(2);
    const float ax[3] = {x.GetX(), x.GetY(), x.GetZ()};
    const float ay[3] = {y.GetX(), y.GetY(), y.GetZ()};
    const float az[3] = {z.GetX(), z.GetY(), z.GetZ()};
    actorEulerFromBasis(ax, ay, az, euler);
}

bool meshHalf(const GpuMeshlet& mesh, const GpuVertex* verts, const uint32_t* indices, float half[3], float center[3]) {
    (void)indices;
    if (verts == nullptr || mesh.vertexCount == 0) {
        return false;
    }
    float minX = 1.0e30f;
    float minY = 1.0e30f;
    float minZ = 1.0e30f;
    float maxX = -1.0e30f;
    float maxY = -1.0e30f;
    float maxZ = -1.0e30f;
    for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
        const GpuVertex& vert = verts[mesh.vertexOffset + v];
        minX = std::min(minX, vert.px);
        minY = std::min(minY, vert.py);
        minZ = std::min(minZ, vert.pz);
        maxX = std::max(maxX, vert.px);
        maxY = std::max(maxY, vert.py);
        maxZ = std::max(maxZ, vert.pz);
    }
    half[0] = std::max((maxX - minX) * 0.5f, 0.06f);
    half[1] = std::max((maxY - minY) * 0.5f, 0.06f);
    half[2] = std::max((maxZ - minZ) * 0.5f, 0.06f);
    center[0] = (minX + maxX) * 0.5f;
    center[1] = (minY + maxY) * 0.5f;
    center[2] = (minZ + maxZ) * 0.5f;
    return true;
}

JPH::BodyID addStaticBox(JPH::BodyInterface& bodies, JPH::Vec3 half, JPH::RVec3 pos) {
    JPH::BoxShapeSettings settings{half};
    const JPH::ShapeSettings::ShapeResult built = settings.Create();
    if (built.HasError()) {
        return {};
    }
    JPH::BodyCreationSettings body{built.Get(), pos, JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::kStatic};
    JPH::Body* created = bodies.CreateBody(body);
    if (created == nullptr) {
        return {};
    }
    bodies.AddBody(created->GetID(), JPH::EActivation::DontActivate);
    return created->GetID();
}

} // namespace

struct DynBody {
    JPH::BodyID id{};
    uint8_t actor = 0xff;
    float half[3]{0.5f, 0.5f, 0.5f};
    float center[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    bool live = false;
};

struct PhysicsWorld {
    BroadPhaseMap broadMap{};
    ObjectVsBroadPhase broadFilter{};
    ObjectPair pairFilter{};
    JPH::TempAllocatorImpl temp{32 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, -1};
    JPH::PhysicsSystem system{};
    JPH::BodyID ground{};
    JPH::BodyID mesh{};
    DynBody dyn[kDynCap]{};
    uint32_t dynCount = 0;
    uint8_t heldLast = 0xff;
    bool open = false;
};

namespace {

bool isDynamicInstance(const PhysicsCook& cook, uint32_t instance) {
    for (uint32_t i = 0; i < cook.dynamicCount; ++i) {
        if (cook.dynamicInstance[i] == instance) {
            return true;
        }
    }
    return false;
}

void addWorldMesh(PhysicsWorld& world, const PhysicsCook& cook) {
    if (cook.verts == nullptr || cook.indices == nullptr || cook.instances == nullptr || cook.meshes == nullptr) {
        return;
    }
    JPH::VertexList vertices;
    JPH::IndexedTriangleList triangles;
    vertices.reserve(65536);
    triangles.reserve(65536);
    for (uint32_t i = 0; i < cook.instanceCount; ++i) {
        if (isDynamicInstance(cook, i)) {
            continue;
        }
        const GpuInstance& inst = cook.instances[i];
        if (inst.meshId >= cook.meshCount) {
            continue;
        }
        const GpuMeshlet& mesh = cook.meshes[inst.meshId];
        if (mesh.vertexCount == 0 || mesh.triangleCount == 0) {
            continue;
        }
        if (vertices.size() + mesh.vertexCount > 12000000u || triangles.size() + mesh.triangleCount * 2u > 24000000u) {
            spdlog::warn("Jolt mesh cap, remaining instances stay on the ground slab");
            break;
        }
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
            const GpuVertex& vert = cook.verts[mesh.vertexOffset + v];
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            instancePoint(inst, vert.px, vert.py, vert.pz, x, y, z);
            vertices.push_back(JPH::Float3(x, y, z));
        }
        for (uint32_t t = 0; t < mesh.triangleCount; ++t) {
            const uint32_t at = mesh.indexOffset + t * 3u;
            const uint32_t i0 = cook.indices[at];
            const uint32_t i1 = cook.indices[at + 1u];
            const uint32_t i2 = cook.indices[at + 2u];
            if (i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount || i0 == i1 || i1 == i2 || i0 == i2) {
                continue;
            }
            triangles.emplace_back(base + i0, base + i1, base + i2);
            triangles.emplace_back(base + i0, base + i2, base + i1);
        }
    }
    if (triangles.empty()) {
        return;
    }
    JPH::MeshShapeSettings settings{vertices, triangles};
    const JPH::ShapeSettings::ShapeResult built = settings.Create();
    if (built.HasError()) {
        spdlog::error("Jolt mesh {}", built.GetError().c_str());
        return;
    }
    JPH::BodyCreationSettings body{built.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::kStatic};
    JPH::BodyInterface& bodies = world.system.GetBodyInterface();
    JPH::Body* created = bodies.CreateBody(body);
    if (created == nullptr) {
        return;
    }
    world.mesh = created->GetID();
    bodies.AddBody(world.mesh, JPH::EActivation::DontActivate);
    spdlog::info("Jolt world {} triangles", static_cast<uint32_t>(triangles.size()));
}

JPH::Quat bodyRotation(const PhysicsPose& pose) {
    return quatFromEuler(pose.euler);
}

JPH::RVec3 bodyPosition(const DynBody& dyn, const PhysicsPose& pose, JPH::Quat rot) {
    const JPH::Vec3 local{dyn.center[0] * pose.scale[0], dyn.center[1] * pose.scale[1], dyn.center[2] * pose.scale[2]};
    const JPH::Vec3 world = rot * local;
    return JPH::RVec3{pose.pos[0] + world.GetX(), pose.pos[1] + world.GetY(), pose.pos[2] + world.GetZ()};
}

void applyScale(JPH::BodyInterface& bodies, DynBody& dyn, const PhysicsPose& pose) {
    const float sx = std::max(pose.scale[0], 0.05f);
    const float sy = std::max(pose.scale[1], 0.05f);
    const float sz = std::max(pose.scale[2], 0.05f);
    if (std::fabs(sx - dyn.scale[0]) < 1.0e-4f && std::fabs(sy - dyn.scale[1]) < 1.0e-4f && std::fabs(sz - dyn.scale[2]) < 1.0e-4f) {
        return;
    }
    const JPH::Vec3 half{std::max(dyn.half[0] * sx, 0.06f), std::max(dyn.half[1] * sy, 0.06f), std::max(dyn.half[2] * sz, 0.06f)};
    JPH::BoxShapeSettings settings{half};
    const JPH::ShapeSettings::ShapeResult built = settings.Create();
    if (built.HasError()) {
        return;
    }
    bodies.SetShape(dyn.id, built.Get(), true, JPH::EActivation::Activate);
    dyn.scale[0] = sx;
    dyn.scale[1] = sy;
    dyn.scale[2] = sz;
}

} // namespace

PhysicsWorld* physicsCreate(const PhysicsCook& cook) {
    joltBoot();
    auto* world = new PhysicsWorld{};
    const uint32_t maxBodies = std::max(cook.instanceCount + kDynCap + 8u, 64u);
    world->system.Init(maxBodies, 0, 65536, 10240, world->broadMap, world->broadFilter, world->pairFilter);
    world->system.SetGravity(JPH::Vec3{0.0f, -9.81f, 0.0f});
    JPH::BodyInterface& bodies = world->system.GetBodyInterface();
    const float minX = cook.boundsMin[0];
    const float minY = cook.boundsMin[1];
    const float minZ = cook.boundsMin[2];
    const float maxX = cook.boundsMax[0];
    const float maxZ = cook.boundsMax[2];
    const float hx = std::max((maxX - minX) * 0.5f + 20.0f, 20.0f);
    const float hz = std::max((maxZ - minZ) * 0.5f + 20.0f, 20.0f);
    world->ground = addStaticBox(bodies, JPH::Vec3{hx, 1.0f, hz}, JPH::RVec3{(minX + maxX) * 0.5f, minY - 1.0f, (minZ + maxZ) * 0.5f});
    addWorldMesh(*world, cook);
    world->open = true;
    for (uint32_t i = 0; i < cook.dynamicCount && world->dynCount < kDynCap; ++i) {
        const uint32_t instance = cook.dynamicInstance[i];
        if (instance >= cook.instanceCount || cook.instances == nullptr || cook.meshes == nullptr) {
            continue;
        }
        const GpuInstance& inst = cook.instances[instance];
        if (inst.meshId >= cook.meshCount) {
            continue;
        }
        physicsAddBox(world, instance, cook.dynamicActor[i], inst, cook.meshes[inst.meshId], cook.verts, cook.indices);
    }
    spdlog::info("Jolt bodies {} dynamic {}", world->system.GetNumBodies(), world->dynCount);
    return world;
}

void physicsDestroy(PhysicsWorld* world) {
    delete world;
}

uint32_t physicsBodyCount(const PhysicsWorld* world) {
    return world != nullptr ? world->dynCount : 0u;
}

uint8_t physicsBodyActor(const PhysicsWorld* world, uint32_t index) {
    if (world == nullptr || index >= world->dynCount) {
        return 0xff;
    }
    return world->dyn[index].actor;
}

bool physicsAddBox(PhysicsWorld* world, uint32_t instance, uint8_t actor, const GpuInstance& inst, const GpuMeshlet& mesh, const GpuVertex* verts, const uint32_t* indices) {
    (void)instance;
    if (world == nullptr || !world->open || world->dynCount >= kDynCap || actor == 0xff) {
        return false;
    }
    float half[3]{0.5f, 0.5f, 0.5f};
    float center[3]{};
    if (!meshHalf(mesh, verts, indices, half, center)) {
        return false;
    }
    float scale[3]{1.0f, 1.0f, 1.0f};
    float axisX[3]{};
    float axisY[3]{};
    float axisZ[3]{};
    float pos[3]{};
    instanceAxes(inst, scale, axisX, axisY, axisZ, pos);
    float euler[3]{};
    actorEulerFromBasis(axisX, axisY, axisZ, euler);
    PhysicsPose pose{};
    pose.pos[0] = pos[0];
    pose.pos[1] = pos[1];
    pose.pos[2] = pos[2];
    pose.euler[0] = euler[0];
    pose.euler[1] = euler[1];
    pose.euler[2] = euler[2];
    pose.scale[0] = scale[0];
    pose.scale[1] = scale[1];
    pose.scale[2] = scale[2];
    const JPH::Vec3 extent{
        std::max(half[0] * scale[0], 0.06f),
        std::max(half[1] * scale[1], 0.06f),
        std::max(half[2] * scale[2], 0.06f),
    };
    JPH::BoxShapeSettings settings{extent};
    const JPH::ShapeSettings::ShapeResult built = settings.Create();
    if (built.HasError()) {
        return false;
    }
    const JPH::Quat rot = bodyRotation(pose);
    DynBody pending{};
    pending.half[0] = half[0];
    pending.half[1] = half[1];
    pending.half[2] = half[2];
    pending.center[0] = center[0];
    pending.center[1] = center[1];
    pending.center[2] = center[2];
    pending.scale[0] = scale[0];
    pending.scale[1] = scale[1];
    pending.scale[2] = scale[2];
    JPH::BodyCreationSettings body{built.Get(), bodyPosition(pending, pose, rot), rot, JPH::EMotionType::Dynamic, Layers::kMoving};
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    body.mFriction = 0.7f;
    body.mRestitution = 0.05f;
    JPH::BodyInterface& bodies = world->system.GetBodyInterface();
    JPH::Body* created = bodies.CreateBody(body);
    if (created == nullptr) {
        return false;
    }
    bodies.AddBody(created->GetID(), JPH::EActivation::Activate);
    DynBody& dyn = world->dyn[world->dynCount];
    dyn = pending;
    dyn.id = created->GetID();
    dyn.actor = actor;
    dyn.live = true;
    world->dynCount += 1u;
    return true;
}

void physicsStep(PhysicsWorld* world, float dt, uint8_t heldActor, PhysicsPose* poses, uint32_t count) {
    if (world == nullptr || !world->open || poses == nullptr || count == 0) {
        return;
    }
    if (count > world->dynCount) {
        count = world->dynCount;
    }
    const float step = dt > 0.05f ? 0.05f : dt;
    JPH::BodyInterface& bodies = world->system.GetBodyInterface();
    for (uint32_t i = 0; i < count; ++i) {
        DynBody& dyn = world->dyn[i];
        if (!dyn.live) {
            continue;
        }
        const bool held = dyn.actor == heldActor;
        const bool released = dyn.actor == world->heldLast && !held;
        const JPH::Quat rot = bodyRotation(poses[i]);
        const JPH::RVec3 want = bodyPosition(dyn, poses[i], rot);
        const JPH::RVec3 have = bodies.GetPosition(dyn.id);
        const float dx = static_cast<float>(want.GetX() - have.GetX());
        const float dy = static_cast<float>(want.GetY() - have.GetY());
        const float dz = static_cast<float>(want.GetZ() - have.GetZ());
        const bool edited = !held && (dx * dx + dy * dy + dz * dz) > 0.0004f;
        if (held || edited) {
            bodies.SetMotionType(dyn.id, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
            applyScale(bodies, dyn, poses[i]);
            bodies.SetPositionAndRotation(dyn.id, want, rot, JPH::EActivation::Activate);
        }
        if (released || edited) {
            bodies.SetMotionType(dyn.id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
            bodies.SetLinearVelocity(dyn.id, JPH::Vec3::sZero());
            bodies.SetAngularVelocity(dyn.id, JPH::Vec3::sZero());
        }
    }
    if (step > 1.0e-4f) {
        world->system.Update(step, 1, &world->temp, &world->jobs);
    }
    for (uint32_t i = 0; i < count; ++i) {
        DynBody& dyn = world->dyn[i];
        if (!dyn.live || dyn.actor == heldActor) {
            continue;
        }
        const JPH::RVec3 pos = bodies.GetPosition(dyn.id);
        const JPH::Quat rot = bodies.GetRotation(dyn.id);
        const JPH::Vec3 local{dyn.center[0] * dyn.scale[0], dyn.center[1] * dyn.scale[1], dyn.center[2] * dyn.scale[2]};
        const JPH::Vec3 shifted = rot * local;
        poses[i].pos[0] = static_cast<float>(pos.GetX() - shifted.GetX());
        poses[i].pos[1] = static_cast<float>(pos.GetY() - shifted.GetY());
        poses[i].pos[2] = static_cast<float>(pos.GetZ() - shifted.GetZ());
        eulerFromQuat(rot, poses[i].euler);
        poses[i].scale[0] = dyn.scale[0];
        poses[i].scale[1] = dyn.scale[1];
        poses[i].scale[2] = dyn.scale[2];
    }
    world->heldLast = heldActor;
}

bool physicsRoof(const PhysicsWorld* world, float x, float y, float z) {
    if (world == nullptr || !world->open) {
        return false;
    }
    const JPH::RRayCast ray{JPH::RVec3(x, y + 0.2, z), JPH::Vec3(0.0f, 22.0f, 0.0f)};
    JPH::RayCastResult hit;
    return world->system.GetNarrowPhaseQuery().CastRay(ray, hit);
}

} // namespace burnhope
