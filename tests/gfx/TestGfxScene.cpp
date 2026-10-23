#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "gpu_scene/FrameUniforms.hpp"
#include "gpu_scene/Camera.hpp"
#include "gpu_scene/InstanceCull.hpp"
#include "gpu_scene/MeshletCull.hpp"
#include "gpu_scene/SunShadow.hpp"
#include "gpu_scene/LightMath.hpp"
#include "gpu_scene/PointShadow.hpp"
#include "render/VisResolve.hpp"
#include "render/PassDebug.hpp"
#include "gfx/PrimitiveGen.hpp"
#include "gfx/MeshletBuilder.hpp"
#include "gfx/Portal.hpp"
#include "scene/FlyCam.hpp"
#include "scene/TestScene.hpp"
#include "gfx/Material.hpp"
#include "gfx/Meshlet.hpp"
#include "gfx/Light.hpp"
#include "gfx/ClusterGrid.hpp"
#include "gfx/HiZ.hpp"
#include "io/File.hpp"
#include "io/Blob.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace burnhope;

TEST_CASE("camera cull feeds frustum cascades and froxels") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 0.0f;
    pose.target[2] = 0.0f;
    pose.aspect = 1.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    CHECK(cameraSphereVisible(cam, 0.0f, 0.0f, 0.0f, 0.2f));
    CHECK(!cameraSphereVisible(cam, 0.0f, 0.0f, 20.0f, 0.2f));
    CHECK(cam.cascadeCount == 4);
    CHECK(cam.cascadeSplit[0] == doctest::Approx(cam.zn));
    CHECK(cam.cascadeSplit[4] == doctest::Approx(cam.zf));
    CHECK(cam.cascadeSplit[1] > cam.cascadeSplit[0]);
    CHECK(cam.froxelCount == 16);
    CHECK(cameraFroxelSlice(cam, cam.zn) == 0);
    CHECK(cameraFroxelSlice(cam, cam.zf) == 15);
    CHECK(cameraConeVisible(cam, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    CHECK(!cameraConeVisible(cam, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.9f));
    CHECK(cam.viewProj.m[11] == doctest::Approx(-1.0f));
}
TEST_CASE("cullInstances keeps spheres inside the frustum") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 0.0f;
    pose.target[2] = 0.0f;
    pose.aspect = 1.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    GpuInstance scene[12]{};
    for (int i = 0; i < 8; ++i) {
        instanceSet(scene[i], static_cast<float>(i) * 0.15f, 0.0f, 0.0f, 0.2f, static_cast<uint32_t>(i), 0);
    }
    for (int i = 0; i < 4; ++i) {
        instanceSet(scene[8 + i], 0.0f, 0.0f, 30.0f, 0.2f, 100, 1);
    }
    uint32_t visible[12]{};
    const uint32_t n = cullInstances(cam, scene, 12, visible, 12);
    CHECK(n == 8);
    for (uint32_t i = 0; i < n; ++i) {
        CHECK(visible[i] == i);
        CHECK(scene[visible[i]].sphere[2] == doctest::Approx(0.0f));
    }
    CHECK(!cameraSphereVisible(cam, scene[8].sphere[0], scene[8].sphere[1], scene[8].sphere[2], scene[8].sphere[3]));
    CHECK(sizeof(GpuInstance) == 80);
    CHECK(n == 8);
}
TEST_CASE("frustum keeps the sphere in front of the camera") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = pose.target[1] = pose.target[2] = 0.0f;
    pose.zn = 0.1f;
    pose.zf = 50.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    InstanceBounds bounds[2]{};
    bounds[0].radius = 0.5f;
    bounds[1].center[2] = 20.0f;
    bounds[1].radius = 0.5f;
    uint32_t visible[2]{};
    const uint32_t n = cullInstances(cam, bounds, 2, visible, 2);
    CHECK(n == 1);
    CHECK(visible[0] == 0);
}
TEST_CASE("visbuffer resolve reads the material by instance id") {
    GpuInstance instance{};
    instanceSet(instance, 0.0f, 0.0f, 0.0f, 1.0f, 2, 0);
    MaterialGpuData materials[3]{};
    materials[2].roughness = 0.3f;
    materials[2].metallic = 0.7f;
    materials[2].baseColor[0] = 0.2f;
    materials[2].baseColor[1] = 0.4f;
    materials[2].baseColor[2] = 0.6f;
    VisHit hit{};
    CHECK(visResolve(visPack(0, 5), &instance, 1, materials, 3, hit));
    CHECK(hit.instanceId == 0);
    CHECK(hit.primitiveId == 5);
    CHECK(hit.materialId == 2);
    CHECK(hit.material.roughness == doctest::Approx(0.3f));
    CHECK(hit.material.metallic == doctest::Approx(0.7f));
    CHECK(hit.material.baseColor[2] == doctest::Approx(0.6f));
}
TEST_CASE("cube meshlet stays inside the vertex and triangle caps") {
    MeshletBlob cube{};
    genCubeMeshlets(1.0f, 1, &cube);
    CHECK(cube.vertexCount == 8);
    CHECK(cube.triangleCount == 12);
    CHECK(cube.vertexCount <= kMeshletMaxVerts);
    CHECK(cube.triangleCount <= kMeshletMaxTris);
    CHECK(cube.meshletCount == 1);
    MeshletBlob plane{};
    genPlaneMeshlets(20.0f, 0, &plane);
    CHECK(plane.vertexCount == 4);
    CHECK(plane.triangleCount == 2);
    CHECK(plane.indices[0] == 0);
    CHECK(plane.indices[1] == 1);
    CHECK(plane.indices[2] == 2);
    CHECK(plane.indices[3] == 2);
    CHECK(plane.indices[4] == 3);
    CHECK(plane.indices[5] == 0);
    CHECK(plane.vertices[0].pos[1] == 0.0f);
    TestScene scene{};
    testSceneBuild(scene);
    CHECK(scene.objectCount == 4);
    CHECK(scene.objects[1].y > 0.6f);
    CHECK(scene.objects[2].y > 0.4f);
    CHECK(scene.objects[3].y > 0.8f);
    CHECK(scene.lightCount == 3);
    ClusterCell cells[kClusterCount]{};
    uint32_t indices[16]{};
    uint32_t indexCount = 0;
    CHECK(clusterAssignLights(cells, indices, 16, indexCount, scene.lights, scene.lightCount));
    const uint32_t a = clusterIndex(2, 1, 3);
    const uint32_t b = clusterIndex(8, 1, 6);
    const uint32_t c = clusterIndex(14, 1, 10);
    CHECK(cells[a].count == 1);
    CHECK(cells[b].count == 1);
    CHECK(cells[c].count == 1);
    CHECK(a != b);
    CHECK(b != c);
    const ScreenBary center = screenBary(1.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 3.0f);
    CHECK(center.flat == false);
    CHECK(center.u == doctest::Approx(1.0f / 3.0f));
    CHECK(center.v == doctest::Approx(1.0f / 3.0f));
    CHECK(center.w == doctest::Approx(1.0f / 3.0f));
    const ScreenBary edge = screenBary(1.5f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 3.0f);
    CHECK(edge.u == doctest::Approx(0.5f));
    CHECK(edge.v == doctest::Approx(0.5f));
    CHECK(edge.w == doctest::Approx(0.0f));
    const ScreenBary outside = screenBary(2.0f, 2.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 3.0f);
    CHECK(outside.u < -0.05f);
    const ScreenBary collapsed = screenBary(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f);
    CHECK(collapsed.flat == true);
    SunShadow sun = sunShadowBuild(cameraCullBuild(CameraPose{}, nullptr), scene.sunDir[0], scene.sunDir[1], scene.sunDir[2]);
    CHECK(sun.resolution == kSunShadowResolution);
    CHECK(sun.viewProj.m[0] != 0.0f);
    uint32_t mip = 0;
    Meshlet facing = scene.cubes[0].meshlets[0];
    facing.center[0] = facing.center[1] = facing.center[2] = 0.0f;
    facing.coneAxis[0] = facing.coneAxis[1] = 0.0f;
    facing.coneAxis[2] = 1.0f;
    facing.coneCutoff = 0.0f;
    facing.radius = 1.0f;
    CameraPose pose{};
    pose.eye[2] = 4.0f;
    CHECK(meshletPassCull(cameraCullBuild(pose, nullptr), facing, 1080.0f, 8, mip));
}
TEST_CASE("sun cascade keeps the slice and reverse depth") {
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 2.0f;
    pose.eye[2] = 0.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 2.0f;
    pose.target[2] = -8.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    const SunShadow sun = sunShadowSlice(cam, 0.0f, -1.0f, 0.0f, 0.2f, 36.0f, 28.0f, 8.0f);
    const auto project = [&](float x, float y, float z, float& u, float& v, float& depth) {
        const float cx = sun.viewProj.m[0] * x + sun.viewProj.m[4] * y + sun.viewProj.m[8] * z + sun.viewProj.m[12];
        const float cy = sun.viewProj.m[1] * x + sun.viewProj.m[5] * y + sun.viewProj.m[9] * z + sun.viewProj.m[13];
        const float cz = sun.viewProj.m[2] * x + sun.viewProj.m[6] * y + sun.viewProj.m[10] * z + sun.viewProj.m[14];
        const float cw = sun.viewProj.m[3] * x + sun.viewProj.m[7] * y + sun.viewProj.m[11] * z + sun.viewProj.m[15];
        const float w = cw == 0.0f ? 1.0f : cw;
        u = cx / w * 0.5f + 0.5f;
        v = cy / w * 0.5f + 0.5f;
        depth = cz / w;
    };
    float u = 0.0f;
    float v = 0.0f;
    float ground = 0.0f;
    float high = 0.0f;
    project(0.0f, 0.0f, -6.0f, u, v, ground);
    CHECK(u > 0.05f);
    CHECK(u < 0.95f);
    CHECK(v > 0.05f);
    CHECK(v < 0.95f);
    project(0.0f, 8.0f, -6.0f, u, v, high);
    CHECK(high > ground);
    CHECK(sun.radius >= 8.0f);
}
TEST_CASE("look straight down keeps a finite view") {
    CameraPose pose{};
    pose.eye[1] = 8.0f;
    pose.target[0] = 0.0f;
    pose.target[1] = 0.0f;
    pose.target[2] = 0.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    CHECK(cam.right[0] == cam.right[0]);
    CHECK(std::fabs(cam.right[0]) > 0.5f);
    CHECK(cam.viewProj.m[0] == cam.viewProj.m[0]);
}
TEST_CASE("meshlet partition stays inside the caps and the bhop header matches") {
    std::vector<float> xyz;
    std::vector<float> nrm;
    std::vector<uint32_t> idx;
    xyz.reserve(500 * 9);
    nrm.reserve(500 * 9);
    idx.reserve(500 * 3);
    for (int i = 0; i < 500; ++i) {
        const float x = static_cast<float>(i);
        const float tri[9] = {x, 0.0f, 0.0f, x + 0.5f, 0.0f, 0.2f, x, 0.0f, 0.4f};
        const float up[9] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (int k = 0; k < 9; ++k) {
            xyz.push_back(tri[k]);
            nrm.push_back(up[k]);
        }
        idx.push_back(static_cast<uint32_t>(i * 3));
        idx.push_back(static_cast<uint32_t>(i * 3 + 1));
        idx.push_back(static_cast<uint32_t>(i * 3 + 2));
    }
    MeshletBuild built{};
    meshletBuildAppend(built, xyz.data(), nrm.data(), idx.data(), static_cast<uint32_t>(idx.size()), 0);
    CHECK(built.meshlets.size() > 1);
    for (const Meshlet& meshlet : built.meshlets) {
        CHECK(meshlet.vertexCount <= kMeshletMaxVerts);
        CHECK(meshlet.triangleCount <= kMeshletMaxTris);
        CHECK(meshlet.vertexCount > 0);
    }
    const float one[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float face[9] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint32_t tri[3] = {0, 1, 2};
    MeshletBuild cone{};
    meshletBuildAppend(cone, one, face, tri, 3, 1);
    CHECK(cone.meshlets.size() == 1);
    const float ax = cone.meshlets[0].center[0];
    const float az = cone.meshlets[0].center[2];
    CHECK(meshletConeVisible(cone.meshlets[0], ax, 5.0f, az));
    CHECK(!meshletConeVisible(cone.meshlets[0], ax, -5.0f, az));
    const char* path = "/tmp/burnhope-meshlet.bhop";
    CHECK(sceneBlobWrite(path, cone.meshlets.data(), 1, cone.vertices.data(), static_cast<uint32_t>(cone.vertices.size()), cone.indices.data(), static_cast<uint32_t>(cone.indices.size()), cone.instances.data(), 1));
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    SceneBlobView view{};
    CHECK(sceneBlobView(mapped.data, mapped.size, &view));
    CHECK(view.info->meshletCount == 1);
    CHECK(view.info->instanceCount == 1);
    CHECK(view.meshlets[0].triangleCount == 1);
    fileUnmap(&mapped);
}
TEST_CASE("a sector stays visible until a portal is marked and leaves the frustum") {
    Sector open{};
    CHECK(sectorVisible(open, nullptr, 0, nullptr));
    Portal door{};
    door.center[0] = 0.0f;
    door.radius = 0.5f;
    const float plane[24] = {
        1.0f, 0.0f, 0.0f, -5.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    Sector hall{};
    hall.portalCount = 1;
    CHECK(!sectorVisible(hall, &door, 1, plane));
    door.center[0] = 10.0f;
    CHECK(sectorVisible(hall, &door, 1, plane));
}
TEST_CASE("fly camera moves forward and yaws with the mouse") {
    FlyCam cam{};
    const float z0 = cam.eye[2];
    InputState input{};
    inputSetBit(input.keyActive, kFlyKeyW);
    flyCamUpdate(cam, input, 1.0f);
    CHECK(cam.eye[2] < z0);
    input = {};
    input.mouseActive = 2;
    input.deltaX = 100.0f;
    input.deltaY = -50.0f;
    const float yaw0 = cam.yaw;
    const float pitch0 = cam.pitch;
    flyCamUpdate(cam, input, 0.016f);
    CHECK(cam.yaw > yaw0);
    CHECK(cam.pitch > pitch0);
    CHECK(cam.pitch < 1.56f);
    const float y0 = cam.eye[1];
    const float z1 = cam.eye[2];
    input = {};
    inputSetBit(input.keyActive, kFlyKeySpace);
    flyCamUpdate(cam, input, 1.0f);
    CHECK(cam.eye[1] > y0);
    CHECK(cam.eye[2] == doctest::Approx(z1));
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    flyCamForward(cam, tx, ty, tz);
    CHECK(ty > 0.0f);
    const float yawBefore = cam.yaw;
    input = {};
    input.mouseActive = 2;
    input.deltaX = 4000.0f;
    flyCamUpdate(cam, input, 0.016f);
    CHECK(cam.yaw - yawBefore == doctest::Approx(240.0f * 0.004f));
}
TEST_CASE("point light shadow face and atlas index") {
    LightGpuData lights[2]{};
    lights[0].type = kLightPoint;
    lights[0].pos[0] = 2.0f;
    lights[0].radius = 4.0f;
    lights[1].type = kLightDirectional;
    pointShadowAssign(lights, 2);
    CHECK(lights[0].shadowMapIndex == 0);
    CHECK(lights[1].shadowMapIndex == -1);
    const Mat4 face = pointShadowFace(lights[0], 0);
    CHECK(face.m[2] != 0.0f);
    const Mat4 upFace = pointShadowFace(lights[0], 2);
    CHECK(upFace.m[0] == upFace.m[0]);
    CHECK(pointShadowDepth(6.0f, 0.0f, 0.0f, lights[0]) == doctest::Approx(1.0f));
}
TEST_CASE("meshlet cone culls a sphere that faces away") {
    MeshletVertex verts[4]{};
    verts[0].pos[2] = 1.0f;
    verts[1].pos[0] = 0.4f;
    verts[1].pos[2] = 0.8f;
    verts[2].pos[0] = -0.4f;
    verts[2].pos[2] = 0.8f;
    verts[3].pos[1] = 0.4f;
    verts[3].pos[2] = 0.8f;
    for (MeshletVertex& v : verts) {
        v.packedNormal = meshletPackNormal(v.pos[0], v.pos[1], v.pos[2]);
    }
    Meshlet meshlet{};
    meshletFit(meshlet, verts, 4);
    CHECK(meshlet.radius > 0.0f);
    CHECK(meshlet.coneAxis[2] > 0.5f);
    CHECK(meshletConeVisible(meshlet, 0.0f, 0.0f, 4.0f));
    CHECK(!meshletConeVisible(meshlet, 0.0f, 0.0f, -4.0f));
    CameraPose pose{};
    pose.eye[0] = 0.0f;
    pose.eye[1] = 0.0f;
    pose.eye[2] = 4.0f;
    pose.target[0] = pose.target[1] = pose.target[2] = 0.0f;
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    CHECK(cameraConeVisible(cam, meshlet.center[0], meshlet.center[1], meshlet.center[2], meshlet.coneAxis[0], meshlet.coneAxis[1], meshlet.coneAxis[2], meshlet.coneCutoff));
    CHECK(hizMipForSphere(1.0f, 10.0f, 1080.0f, 1.0f, 8) >= 6);
    const float mip[4] = {0.4f, 0.4f, 0.4f, 0.4f};
    CHECK(hizSphereVisible(mip, 2, 2, 0.5f, 0.5f, 0.2f));
    CHECK(!hizSphereVisible(mip, 2, 2, 0.5f, 0.5f, 0.9f));
}
TEST_CASE("cluster grid keeps a point light in its sphere") {
    LightGpuData light{};
    light.type = kLightPoint;
    light.pos[0] = 8.5f;
    light.pos[1] = 4.5f;
    light.pos[2] = 12.5f;
    light.radius = 0.25f;
    ClusterCell cells[kClusterCount]{};
    uint32_t indices[8]{};
    uint32_t indexCount = 0;
    CHECK(clusterAssignLights(cells, indices, 8, indexCount, &light, 1));
    const uint32_t hit = clusterIndex(8, 4, 12);
    CHECK(cells[hit].count == 1);
    CHECK(indices[cells[hit].offset] == 0);
    CHECK(cells[clusterIndex(0, 0, 0)].count == 0);
    CHECK(indexCount == 1);
}
TEST_CASE("meshlet blob maps without a copy") {
    Meshlet meshlet{};
    meshlet.center[0] = 1.0f;
    meshlet.radius = 2.0f;
    meshlet.vertexCount = 3;
    uint8_t blob[sizeof(AssetHeader) + sizeof(Meshlet)]{};
    const size_t bytes = meshletSerialize(&meshlet, 1, blob, sizeof(blob));
    CHECK(bytes == sizeof(blob));
    const char* path = "/tmp/burnhope-meshlet.bin";
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(blob, 1, bytes, file) == bytes);
    std::fclose(file);
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    uint32_t count = 0;
    const Meshlet* view = meshletView(mapped.data, mapped.size, count);
    CHECK(view != nullptr);
    CHECK(count == 1);
    CHECK(view->radius == doctest::Approx(2.0f));
    CHECK(view->vertexCount == 3);
    CHECK(reinterpret_cast<const uint8_t*>(view) == mapped.data + sizeof(AssetHeader));
    fileUnmap(&mapped);
    std::remove(path);
}
TEST_CASE("material blob roundtrips through the asset header") {
    MaterialGpuData mat{};
    mat.baseColor[0] = 0.2f;
    mat.baseColor[1] = 0.4f;
    mat.baseColor[2] = 0.6f;
    mat.roughness = 0.35f;
    mat.metallic = 0.8f;
    mat.albedoTexIndex = 3;
    uint8_t blob[sizeof(AssetHeader) + sizeof(MaterialGpuData)]{};
    const size_t bytes = materialSerialize(mat, blob, sizeof(blob));
    CHECK(bytes == sizeof(blob));
    AssetHeader header{};
    std::memcpy(&header, blob, sizeof(header));
    CHECK(header.magic == kAssetMagic);
    CHECK(header.version == 1);
    CHECK(header.typeHash == assetHash("Material"));
    CHECK(header.dataSize == sizeof(MaterialGpuData));
    MaterialGpuData back{};
    CHECK(materialDeserialize(blob, bytes, &back));
    CHECK(std::memcmp(&mat, &back, sizeof(mat)) == 0);
    blob[sizeof(AssetHeader)] ^= 1;
    CHECK(!materialDeserialize(blob, bytes, &back));
}
TEST_CASE("a 64-byte material record keeps the normal flip and the emissive tint") {
    MaterialGpuData src{};
    src.flags = 0;
    src.baseColor[0] = 0.2f;
    src.baseColor[1] = 0.3f;
    src.baseColor[2] = 0.4f;
    src.emissiveIntensity = 3.0f;
    uint8_t payload[kMaterialGpuV9]{};
    std::memcpy(payload, &src, kMaterialGpuV9);
    AssetHeader header{};
    header.magic = kAssetMagic;
    header.version = 1;
    header.typeHash = assetHash("Material");
    header.dataSize = kMaterialGpuV9;
    header.checksum = assetHashBytes(payload, kMaterialGpuV9);
    uint8_t blob[sizeof(AssetHeader) + kMaterialGpuV9]{};
    std::memcpy(blob, &header, sizeof(header));
    std::memcpy(blob + sizeof(header), payload, kMaterialGpuV9);
    MaterialGpuData adopted{};
    CHECK(materialDeserialize(blob, sizeof(blob), &adopted));
    CHECK((adopted.flags & kMatFlipNormalY) != 0u);
    CHECK(adopted.emissiveGain == doctest::Approx(3.0f));
    CHECK(adopted.emissiveColor[0] == doctest::Approx(0.2f));
    CHECK(adopted.uvTiling[0] == doctest::Approx(1.0f));
}
TEST_CASE("bhmat json keeps scalars and texture paths") {
    BhmatDoc doc{};
    doc.base[0] = 0.25f;
    doc.roughness = 0.4f;
    doc.worldAligned = 1;
    doc.worldGridSize = 2.5f;
    doc.uvRotation = 0.5f;
    doc.triplanarSharpness = 6.0f;
    std::snprintf(doc.albedoPath, sizeof(doc.albedoPath), "textures/brick.png");
    const char* path = "/tmp/burnhope-mat-test.bhmat";
    CHECK(materialSaveBhmat(path, doc));
    BhmatDoc back{};
    CHECK(materialLoadBhmat(path, back));
    CHECK(back.base[0] == doctest::Approx(0.25f));
    CHECK(back.roughness == doctest::Approx(0.4f));
    CHECK(back.worldAligned == 1);
    CHECK(back.worldGridSize == doctest::Approx(2.5f));
    CHECK(back.uvRotation == doctest::Approx(0.5f));
    CHECK(back.triplanarSharpness == doctest::Approx(6.0f));
    CHECK(std::strcmp(back.albedoPath, "textures/brick.png") == 0);
    std::remove(path);
}
TEST_CASE("light math keeps energy penumbra and contact handoff") {
    const float smooth = hammonDiffuse(1.0f, 1.0f, 1.0f, 0.0f);
    const float rough = hammonDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(smooth > 0.2f);
    CHECK(rough > 0.0f);
    CHECK(coatLeft(1.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(coatLeft(0.0f, 1.0f) < 0.1f);
    CHECK(pcssPenumbra(0.5f, 0.5f, 1.0f) == doctest::Approx(1.0f));
    CHECK(pcssPenumbra(0.8f, 0.2f, 1.0f) > 1.0f);
    float x = 0.0f;
    float y = 0.0f;
    vogelDisk(0, 12, 0.0f, x, y);
    CHECK(x * x + y * y < 1.0f);
    float differ = 0.0f;
    const float n0 = blueNoise64(0, 0);
    for (uint32_t i = 1; i < 16; ++i) {
        differ += std::fabs(blueNoise64(i, 3) - n0);
    }
    CHECK(differ > 0.1f);
    CHECK(sscsHandoff(0.02f) == doctest::Approx(0.0f));
    CHECK(sscsHandoff(0.125f) == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(sscsHandoff(0.4f) == doctest::Approx(1.0f));
    CHECK(casDelta(1.0f, 0.5f, 0.25f) == doctest::Approx(0.125f));
    CHECK(froxelSlice(0.1f, 80.0f, 0, 16) == doctest::Approx(0.1f));
    CHECK(froxelSlice(0.1f, 80.0f, 16, 16) == doctest::Approx(80.0f));
    CHECK(ggxCone(0.0f) == 0.0f);
    CHECK(ggxCone(1.0f) > ggxCone(0.5f));
    float ox = 0.0f;
    float oy = 0.0f;
    float oz = 0.0f;
    planarReflect(0.2f, -0.4f, 0.6f, ox, oy, oz);
    CHECK(ox == -0.2f);
    CHECK(oy == -0.4f);
    CHECK(oz == -0.6f);
    CHECK(ltcForm(0.0f) == 0.0f);
    CHECK(ltcForm(1.0f) == 0.5f);
    CHECK(outdoorProbe(4u, 1u, 2u) == 1u + 3u * 4u + 2u * 16u);
    CHECK(cloudNoiseIndex(1u, 2u, 3u, 128u, 0u) == 1u + 2u * 128u + 3u * 128u * 128u);
    CHECK(cloudNoiseIndex(0u, 0u, 0u, 32u, 128u * 128u * 128u) == 128u * 128u * 128u);
    CHECK(shadowPagesCrossed(3, 4, 3, 4) == 0u);
    CHECK(shadowPagesCrossed(3, 4, 5, 4) == 3u);
    CHECK(spotPage(40.0f) == 64u);
    CHECK(spotPage(200.0f) == 512u);
    CHECK(dominantFace(0.0f, -2.0f, 0.1f) == 3u);
    CHECK(ltcCorner(0.0f, 0.0f, 1.0f, 1.0f) == 0.5f);
    CHECK(shadowPage(128, 256, 128) == 2u * (4096u / 128u) + 1u);
    CHECK(sizeof(AtmosphereParams) == 192);
    CHECK(sizeof(BloomParams) == 64);
}
