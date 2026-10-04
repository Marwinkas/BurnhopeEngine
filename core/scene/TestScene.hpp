#pragma once

#include "gfx/ClusterGrid.hpp"
#include "gfx/Light.hpp"
#include "gfx/Material.hpp"
#include "gfx/PrimitiveGen.hpp"

#include <cstdint>

namespace burnhope {

struct SceneObject {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 1.0f;
    uint32_t materialId = 0;
    uint32_t meshId = 0;
};

struct TestScene {
    MeshletBlob floor{};
    MeshletBlob cubes[3]{};
    SceneObject objects[4]{};
    uint32_t objectCount = 0;
    MaterialGpuData materials[4]{};
    uint32_t materialCount = 0;
    LightGpuData lights[4]{};
    uint32_t lightCount = 0;
    float sunDir[3]{-0.4f, -1.0f, -0.3f};
    float eye[3]{0.0f, 2.0f, 8.0f};
    float target[3]{0.0f, 1.0f, 0.0f};
};

inline void testSceneBuild(TestScene& scene) {
    scene = {};
    genPlaneMeshlets(20.0f, 0, &scene.floor);
    genCubeMeshlets(1.2f, 1, &scene.cubes[0]);
    genCubeMeshlets(0.8f, 2, &scene.cubes[1]);
    genCubeMeshlets(1.6f, 3, &scene.cubes[2]);
    scene.materials[0].roughness = 0.9f;
    scene.materials[0].metallic = 0.0f;
    scene.materials[0].baseColor[0] = 0.35f;
    scene.materials[0].baseColor[1] = 0.34f;
    scene.materials[0].baseColor[2] = 0.32f;
    scene.materials[1].roughness = 0.4f;
    scene.materials[1].baseColor[0] = 0.8f;
    scene.materials[1].baseColor[1] = 0.25f;
    scene.materials[1].baseColor[2] = 0.2f;
    scene.materials[2].roughness = 0.25f;
    scene.materials[2].metallic = 0.8f;
    scene.materials[2].baseColor[0] = 0.2f;
    scene.materials[2].baseColor[1] = 0.45f;
    scene.materials[2].baseColor[2] = 0.85f;
    scene.materials[3].roughness = 0.55f;
    scene.materials[3].baseColor[0] = 0.85f;
    scene.materials[3].baseColor[1] = 0.75f;
    scene.materials[3].baseColor[2] = 0.3f;
    scene.materialCount = 4;
    scene.objects[0] = {0.0f, 0.0f, 0.0f, 14.0f, 0, 0};
    scene.objects[1] = {-1.5f, 0.62f, 0.0f, 1.2f, 1, 1};
    scene.objects[2] = {1.2f, 0.42f, -1.0f, 0.8f, 2, 2};
    scene.objects[3] = {0.4f, 0.82f, 1.6f, 1.6f, 3, 3};
    scene.objectCount = 4;
    scene.lights[0].type = kLightPoint;
    scene.lights[0].pos[0] = 2.5f;
    scene.lights[0].pos[1] = 1.5f;
    scene.lights[0].pos[2] = 3.5f;
    scene.lights[0].radius = 0.2f;
    scene.lights[0].color[0] = 1.0f;
    scene.lights[0].color[1] = 0.4f;
    scene.lights[0].color[2] = 0.2f;
    scene.lights[1].type = kLightPoint;
    scene.lights[1].pos[0] = 8.5f;
    scene.lights[1].pos[1] = 1.5f;
    scene.lights[1].pos[2] = 6.5f;
    scene.lights[1].radius = 0.2f;
    scene.lights[1].color[0] = 0.2f;
    scene.lights[1].color[1] = 0.6f;
    scene.lights[1].color[2] = 1.0f;
    scene.lights[2].type = kLightPoint;
    scene.lights[2].pos[0] = 14.5f;
    scene.lights[2].pos[1] = 1.5f;
    scene.lights[2].pos[2] = 10.5f;
    scene.lights[2].radius = 0.2f;
    scene.lights[2].color[0] = 0.3f;
    scene.lights[2].color[1] = 1.0f;
    scene.lights[2].color[2] = 0.4f;
    scene.lightCount = 3;
    scene.sunDir[0] = -0.4f;
    scene.sunDir[1] = -1.0f;
    scene.sunDir[2] = -0.3f;
    scene.eye[0] = 0.0f;
    scene.eye[1] = 2.0f;
    scene.eye[2] = 8.0f;
    scene.target[1] = 1.0f;
}

} // namespace burnhope
