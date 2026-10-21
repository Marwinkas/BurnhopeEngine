#pragma once

#include "gpu_scene/InstanceData.hpp"
#include "gpu_scene/MeshletGpu.hpp"

#include <cstdint>

namespace burnhope {

struct PhysicsWorld;

struct PhysicsPose {
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
};

struct PhysicsCook {
    const GpuVertex* verts = nullptr;
    const uint32_t* indices = nullptr;
    const GpuInstance* instances = nullptr;
    const GpuMeshlet* meshes = nullptr;
    uint32_t instanceCount = 0;
    uint32_t meshCount = 0;
    const uint32_t* dynamicInstance = nullptr;
    const uint8_t* dynamicActor = nullptr;
    uint32_t dynamicCount = 0;
    float boundsMin[3]{};
    float boundsMax[3]{};
};

[[nodiscard]] PhysicsWorld* physicsCreate(const PhysicsCook& cook);
void physicsDestroy(PhysicsWorld* world);

[[nodiscard]] uint32_t physicsBodyCount(const PhysicsWorld* world);
[[nodiscard]] uint8_t physicsBodyActor(const PhysicsWorld* world, uint32_t index);

bool physicsAddBox(PhysicsWorld* world, uint32_t instance, uint8_t actor, const GpuInstance& inst, const GpuMeshlet& mesh, const GpuVertex* verts, const uint32_t* indices);

// heldActor == 0xff: every box is dynamic. Otherwise that actor is glued to its pose.
void physicsStep(PhysicsWorld* world, float dt, uint8_t heldActor, PhysicsPose* poses, uint32_t count);

} // namespace burnhope
