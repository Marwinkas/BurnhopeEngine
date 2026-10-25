#include "render/Physics.hpp"
#include "render/SceneFrameImpl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace burnhope {

namespace {

void writeCube(GpuVertex* vert, uint32_t* idx, float half) {
    struct Face {
        float cx, cy, cz, ux, uy, uz, vx, vy, vz;
    };
    const Face faces[6] = {
        {0.0f, half, 0.0f, half, 0.0f, 0.0f, 0.0f, 0.0f, -half},
        {0.0f, -half, 0.0f, half, 0.0f, 0.0f, 0.0f, 0.0f, half},
        {half, 0.0f, 0.0f, 0.0f, half, 0.0f, 0.0f, 0.0f, half},
        {-half, 0.0f, 0.0f, 0.0f, half, 0.0f, 0.0f, 0.0f, -half},
        {0.0f, 0.0f, half, half, 0.0f, 0.0f, 0.0f, half, 0.0f},
        {0.0f, 0.0f, -half, half, 0.0f, 0.0f, 0.0f, -half, 0.0f},
    };
    const float corner[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    const uint32_t local[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    uint32_t vcount = 0;
    uint32_t icount = 0;
    for (const Face& face : faces) {
        float nx = face.uy * face.vz - face.uz * face.vy;
        float ny = face.uz * face.vx - face.ux * face.vz;
        float nz = face.ux * face.vy - face.uy * face.vx;
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen > 1.0e-6f) {
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;
        } else {
            ny = 1.0f;
        }
        const uint32_t faceBase = vcount;
        for (uint32_t c = 0; c < 4u; ++c) {
            GpuVertex& v = vert[vcount];
            v = {};
            v.px = face.cx + corner[c][0] * face.ux + corner[c][1] * face.vx;
            v.py = face.cy + corner[c][0] * face.uy + corner[c][1] * face.vy;
            v.pz = face.cz + corner[c][0] * face.uz + corner[c][1] * face.vz;
            v.pad0 = corner[c][0] * 0.5f + 0.5f;
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;
            v.pad1 = corner[c][1] * 0.5f + 0.5f;
            vcount += 1;
        }
        for (uint32_t t = 0; t < 6u; ++t) {
            idx[icount] = faceBase + local[t];
            icount += 1;
        }
    }
}

void writePlane(GpuVertex* vert, uint32_t* idx) {
    writeCube(vert, idx, 0.02f);
    for (uint32_t i = 0; i < 24u; ++i) {
        vert[i].py = 0.0f;
        vert[i].ny = 1.0f;
        vert[i].nx = 0.0f;
        vert[i].nz = 0.0f;
    }
}

void writeSphereMesh(GpuVertex* vert, uint32_t* idx) {
    constexpr int kSlice = 8;
    constexpr int kStack = 4;
    uint32_t vcount = 0;
    for (int y = 0; y <= kStack; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(kStack);
        const float pitch = (v - 0.5f) * 3.14159265f;
        const float cy = std::cos(pitch);
        const float sy = std::sin(pitch);
        for (int x = 0; x <= kSlice; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(kSlice);
            const float yaw = u * 6.2831853f;
            GpuVertex& dst = vert[vcount++];
            dst = {};
            dst.px = std::cos(yaw) * cy * 0.5f;
            dst.py = sy * 0.5f;
            dst.pz = std::sin(yaw) * cy * 0.5f;
            dst.nx = dst.px * 2.0f;
            dst.ny = dst.py * 2.0f;
            dst.nz = dst.pz * 2.0f;
            dst.pad0 = u;
            dst.pad1 = v;
        }
    }
    uint32_t icount = 0;
    for (int y = 0; y < kStack; ++y) {
        for (int x = 0; x < kSlice; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * (kSlice + 1) + x);
            const uint32_t b = a + static_cast<uint32_t>(kSlice + 1);
            idx[icount++] = a;
            idx[icount++] = b;
            idx[icount++] = a + 1u;
            idx[icount++] = a + 1u;
            idx[icount++] = b;
            idx[icount++] = b + 1u;
        }
    }
}

void writeCylinder(GpuVertex* vert, uint32_t* idx) {
    constexpr int kSide = 8;
    uint32_t vcount = 0;
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? -0.5f : 0.5f;
        for (int i = 0; i < kSide; ++i) {
            const float ang = static_cast<float>(i) * 6.2831853f / static_cast<float>(kSide);
            GpuVertex& dst = vert[vcount++];
            dst = {};
            dst.px = std::cos(ang) * 0.5f;
            dst.py = y;
            dst.pz = std::sin(ang) * 0.5f;
            dst.nx = dst.px * 2.0f;
            dst.ny = 0.0f;
            dst.nz = dst.pz * 2.0f;
        }
    }
    GpuVertex& bottom = vert[vcount++];
    bottom = {};
    bottom.py = -0.5f;
    bottom.ny = -1.0f;
    GpuVertex& top = vert[vcount++];
    top = {};
    top.py = 0.5f;
    top.ny = 1.0f;
    uint32_t icount = 0;
    for (int i = 0; i < kSide; ++i) {
        const uint32_t n = static_cast<uint32_t>((i + 1) % kSide);
        idx[icount++] = static_cast<uint32_t>(i);
        idx[icount++] = n;
        idx[icount++] = static_cast<uint32_t>(kSide) + n;
        idx[icount++] = static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide) + n;
        idx[icount++] = static_cast<uint32_t>(kSide) + static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide * 2);
        idx[icount++] = n;
        idx[icount++] = static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide * 2 + 1);
        idx[icount++] = static_cast<uint32_t>(kSide) + static_cast<uint32_t>(i);
        idx[icount++] = static_cast<uint32_t>(kSide) + n;
    }
}

} // namespace

void sceneFramePlantCubes(SceneFrame* scene) {
    if (scene == nullptr || scene->vertSrc == nullptr || scene->instanceCount == 0 || scene->indexSrc == nullptr
        || scene->meshletSrc == nullptr) {
        return;
    }
    const uint32_t v0 = scene->vertCount;
    const uint32_t i0 = scene->indexCount;
    const uint32_t n0 = scene->instanceCount;
    const uint32_t m0 = scene->materialCount;
    uint32_t mesh0 = n0;
    if (scene->meshletSrc == scene->meshletGpu.data() && !scene->meshletGpu.empty()) {
        mesh0 = static_cast<uint32_t>(scene->meshletGpu.size());
    }
    constexpr uint32_t kExtraN = 5u;
    std::vector<GpuVertex> verts;
    std::vector<uint32_t> indices;
    std::vector<GpuInstance> instances(n0 + kExtraN);
    std::vector<GpuMeshlet> meshlets(std::max(mesh0, n0) + kExtraN);
    std::vector<MaterialGpuData> materials(m0 + kExtraN);
    verts.assign(scene->vertSrc, scene->vertSrc + v0);
    indices.assign(scene->indexSrc, scene->indexSrc + i0);
    std::memcpy(instances.data(), scene->instSrc, sizeof(GpuInstance) * n0);
    const uint32_t meshCopy = mesh0 < n0 ? mesh0 : n0;
    std::memcpy(meshlets.data(), scene->meshletSrc, sizeof(GpuMeshlet) * meshCopy);
    if (scene->materialSrc != nullptr && m0 > 0) {
        std::memcpy(materials.data(), scene->materialSrc, sizeof(MaterialGpuData) * m0);
    }
    auto paint = [&](uint32_t slot, float r, float g, float b, float rough, float metal) {
        MaterialGpuData& mat = materials[m0 + slot];
        mat = {};
        mat.baseColor[0] = r;
        mat.baseColor[1] = g;
        mat.baseColor[2] = b;
        mat.baseColor[3] = 1.0f;
        mat.roughness = rough;
        mat.metallic = metal;
    };
    paint(0, 0.45f, 0.45f, 0.43f, 0.9f, 0.0f);
    paint(1, 0.82f, 0.16f, 0.08f, 0.45f, 0.0f);
    paint(2, 0.16f, 0.38f, 0.82f, 0.25f, 0.8f);
    paint(3, 0.86f, 0.72f, 0.18f, 0.55f, 0.0f);
    paint(4, 1.0f, 1.0f, 1.0f, 0.04f, 1.0f);
    const float ox = scene->fly.eye[0];
    const float oy = scene->boxMin[1];
    const float oz = scene->fly.eye[2] - 10.0f;
    uint32_t added = 0;
    auto finish = [&](float cx, float cy, float cz, float radius, uint32_t matSlot, uint32_t base, uint32_t ind, uint32_t vcount, uint32_t tris) {
        const uint32_t inst = n0 + added;
        GpuMeshlet& mesh = meshlets[inst];
        mesh = {};
        mesh.vertexOffset = base;
        mesh.indexOffset = ind;
        mesh.vertexCount = vcount;
        mesh.triangleCount = tris;
        mesh.coneCutoff = -1.0f;
        instanceSet(instances[inst], cx, cy, cz, radius, m0 + matSlot, inst);
        added += 1;
    };
    auto emitBox = [&](float cx, float cy, float cz, float hx, float hy, float hz, uint32_t matSlot) {
        struct Face {
            float cx, cy, cz, ux, uy, uz, vx, vy, vz;
        };
        const Face faces[6] = {
            {0.0f, hy, 0.0f, hx, 0.0f, 0.0f, 0.0f, 0.0f, -hz},
            {0.0f, -hy, 0.0f, hx, 0.0f, 0.0f, 0.0f, 0.0f, hz},
            {hx, 0.0f, 0.0f, 0.0f, hy, 0.0f, 0.0f, 0.0f, hz},
            {-hx, 0.0f, 0.0f, 0.0f, hy, 0.0f, 0.0f, 0.0f, -hz},
            {0.0f, 0.0f, hz, hx, 0.0f, 0.0f, 0.0f, hy, 0.0f},
            {0.0f, 0.0f, -hz, hx, 0.0f, 0.0f, 0.0f, -hy, 0.0f},
        };
        const float corner[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        const uint32_t base = static_cast<uint32_t>(verts.size());
        const uint32_t ind = static_cast<uint32_t>(indices.size());
        for (const Face& face : faces) {
            float nx = face.uy * face.vz - face.uz * face.vy;
            float ny = face.uz * face.vx - face.ux * face.vz;
            float nz = face.ux * face.vy - face.uy * face.vx;
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen > 1.0e-6f) {
                nx /= nlen;
                ny /= nlen;
                nz /= nlen;
            } else {
                ny = 1.0f;
            }
            const uint32_t faceBase = static_cast<uint32_t>(verts.size()) - base;
            for (uint32_t c = 0; c < 4; ++c) {
                GpuVertex v{};
                v.px = face.cx + corner[c][0] * face.ux + corner[c][1] * face.vx;
                v.py = face.cy + corner[c][0] * face.uy + corner[c][1] * face.vy;
                v.pz = face.cz + corner[c][0] * face.uz + corner[c][1] * face.vz;
                v.nx = nx;
                v.ny = ny;
                v.nz = nz;
                verts.push_back(v);
            }
            const uint32_t local[6] = {0, 1, 2, 0, 2, 3};
            for (uint32_t t = 0; t < 6; ++t) {
                indices.push_back(faceBase + local[t]);
            }
        }
        const float radius = std::sqrt(hx * hx + hy * hy + hz * hz);
        finish(cx, cy, cz, radius, matSlot, base, ind, 24, 12);
    };
    const uint32_t floorBase = static_cast<uint32_t>(verts.size());
    const uint32_t floorInd = static_cast<uint32_t>(indices.size());
    const float floorN[4][3] = {{-3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, 3.0f}, {-3.0f, 0.0f, 3.0f}};
    for (uint32_t i = 0; i < 4; ++i) {
        GpuVertex v{};
        v.px = floorN[i][0];
        v.py = floorN[i][1];
        v.pz = floorN[i][2];
        v.ny = 1.0f;
        verts.push_back(v);
    }
    const uint32_t floorTri[6] = {0, 2, 1, 0, 3, 2};
    for (uint32_t t = 0; t < 6; ++t) {
        indices.push_back(floorTri[t]);
    }
    finish(ox, oy + 0.02f, oz, 5.0f, 0, floorBase, floorInd, 4, 2);
    emitBox(ox - 1.6f, oy + 2.4f, oz, 0.55f, 0.55f, 0.55f, 1);
    emitBox(ox + 1.5f, oy + 3.6f, oz - 1.1f, 0.4f, 0.4f, 0.4f, 2);
    emitBox(ox + 0.1f, oy + 5.0f, oz + 1.3f, 0.7f, 0.7f, 0.7f, 3);
    float mirrorX = ox + 2.8f;
    float mirrorY = oy + 0.7f;
    float mirrorZ = oz;
    int bestRank = 0;
    float bestDist = 1.0e30f;
    const float eyeX = scene->fly.eye[0];
    const float eyeZ = scene->fly.eye[2];
    for (uint32_t i = 0; i < n0; ++i) {
        const uint32_t matId = instances[i].materialId;
        if (matId >= m0) {
            continue;
        }
        const float radius = instances[i].sphere[3];
        if (radius < 0.8f || radius > 22.0f) {
            continue;
        }
        bool namedTree = false;
        if (scene->texNames != nullptr) {
            const uint32_t tex = materials[matId].albedoTexIndex;
            if (tex > 0u && tex < scene->texNameCount) {
                const char* file = scene->texNames[tex].file;
                namedTree = nameHas(file, "tree") || nameHas(file, "linden") || (file[0] == 'd' && file[1] == '/');
            }
        }
        const bool foliage = (materials[matId].flags & 32u) != 0u;
        const int rank = namedTree ? 2 : (foliage ? 1 : 0);
        if (rank == 0) {
            continue;
        }
        const float dx = instances[i].sphere[0] - eyeX;
        const float dz = instances[i].sphere[2] - eyeZ;
        const float dist = dx * dx + dz * dz;
        if (rank < bestRank || (rank == bestRank && dist >= bestDist)) {
            continue;
        }
        float trunkX = instances[i].sphere[0];
        float trunkZ = instances[i].sphere[2];
        float bottom = instances[i].sphere[1] - radius;
        const uint32_t meshId = instances[i].meshId;
        if (meshId < meshlets.size()) {
            const GpuMeshlet& mesh = meshlets[meshId];
            const uint32_t end = mesh.vertexOffset + mesh.vertexCount;
            if (mesh.vertexCount > 0u && end <= verts.size()) {
                float low = 1.0e9f;
                for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
                    low = std::min(low, verts[mesh.vertexOffset + v].py);
                }
                float ax = 0.0f;
                float az = 0.0f;
                float n = 0.0f;
                for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
                    const GpuVertex& vert = verts[mesh.vertexOffset + v];
                    if (vert.py > low + 0.35f) {
                        continue;
                    }
                    ax += vert.px;
                    az += vert.pz;
                    n += 1.0f;
                }
                if (n > 0.0f) {
                    trunkX = ax / n;
                    trunkZ = az / n;
                    bottom = low;
                }
            }
        }
        const float ground = scene->boxMin[1];
        if (bottom > ground + 0.5f && bottom < ground + 15.0f) {
            bottom = ground;
        }
        float sx = eyeX - trunkX;
        float sz = eyeZ - trunkZ;
        const float sl = std::sqrt(sx * sx + sz * sz);
        if (sl > 0.5f) {
            sx /= sl;
            sz /= sl;
        } else {
            sx = 1.0f;
            sz = 0.0f;
        }
        const float sideX = -sz;
        const float sideZ = sx;
        bestRank = rank;
        bestDist = dist;
        mirrorX = trunkX + sideX * 2.0f + sx * 1.2f;
        mirrorY = bottom + 0.7f;
        mirrorZ = trunkZ + sideZ * 2.0f + sz * 1.2f;
    }
    emitBox(mirrorX, oy + 6.4f, mirrorZ, 0.7f, 0.7f, 0.7f, 4);
    const SceneTexName* texNames = scene->texNames;
    const uint32_t texCount = scene->texNameCount;
    scene->ownedVert = std::move(verts);
    scene->ownedIndex = std::move(indices);
    scene->ownedInst = std::move(instances);
    scene->ownedMesh = std::move(meshlets);
    scene->ownedMat = std::move(materials);
    scene->vertSrc = scene->ownedVert.data();
    scene->indexSrc = scene->ownedIndex.data();
    scene->instSrc = scene->ownedInst.data();
    scene->meshletSrc = scene->ownedMesh.data();
    scene->materialSrc = scene->ownedMat.data();
    scene->texNames = texNames;
    scene->texNameCount = texCount;
    scene->vertCount = static_cast<uint32_t>(scene->ownedVert.size());
    scene->indexCount = static_cast<uint32_t>(scene->ownedIndex.size());
    scene->instanceCount = n0 + added;
    scene->materialCount = m0 + kExtraN;
    scene->visibleCpu.assign(scene->instanceCount, 0u);
    scene->visibleCap = scene->instanceCount;
    scene->meshletGpu.clear();
    scene->cones = nullptr;
    scene->coneCount = 0;
    scene->boxMin[0] = std::min(scene->boxMin[0], std::min(ox - 6.0f, mirrorX - 1.0f));
    scene->boxMin[1] = std::min(scene->boxMin[1], std::min(oy - 1.0f, mirrorY - 1.0f));
    scene->boxMin[2] = std::min(scene->boxMin[2], std::min(oz - 6.0f, mirrorZ - 1.0f));
    scene->boxMax[0] = std::max(scene->boxMax[0], std::max(ox + 6.0f, mirrorX + 1.0f));
    scene->boxMax[1] = std::max(scene->boxMax[1], std::max(oy + 3.0f, mirrorY + 1.0f));
    scene->boxMax[2] = std::max(scene->boxMax[2], std::max(oz + 6.0f, mirrorZ + 1.0f));
    linkProp(*scene, n0 + 1u, "Cube");
    linkProp(*scene, n0 + 2u, "Cube 2");
    linkProp(*scene, n0 + 3u, "Cube 3");
    linkProp(*scene, n0 + 4u, "Metal");
    scene->actors.selected = 0;
    armEditorRoom(*scene);
    scene->ready = false;
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    uint32_t dynamicInstance[kPropCap]{};
    uint8_t dynamicActor[kPropCap]{};
    uint32_t dynamicCount = 0;
    for (uint8_t n = 0; n < scene->propCount && dynamicCount < kPropCap; ++n) {
        dynamicInstance[dynamicCount] = scene->props[n].instance;
        dynamicActor[dynamicCount] = scene->props[n].actor;
        dynamicCount += 1u;
    }
    PhysicsCook cook{};
    cook.verts = scene->vertSrc;
    cook.indices = scene->indexSrc;
    cook.instances = scene->instSrc;
    cook.meshes = scene->meshletSrc;
    cook.instanceCount = scene->instanceCount;
    cook.meshCount = static_cast<uint32_t>(scene->ownedMesh.size());
    cook.dynamicInstance = dynamicInstance;
    cook.dynamicActor = dynamicActor;
    cook.dynamicCount = dynamicCount;
    cook.boundsMin[0] = scene->boxMin[0];
    cook.boundsMin[1] = scene->boxMin[1];
    cook.boundsMin[2] = scene->boxMin[2];
    cook.boundsMax[0] = scene->boxMax[0];
    cook.boundsMax[1] = scene->boxMax[1];
    cook.boundsMax[2] = scene->boxMax[2];
    scene->physics = physicsCreate(cook);
    bakeAudio(*scene);
}

uint8_t sceneFrameSpawn(SceneFrame* scene, uint8_t kind) {
    if (scene == nullptr) {
        return kSceneActorNone;
    }
    if (kind >= 4) {
        float fx = 0.0f;
        float fy = 0.0f;
        float fz = -1.0f;
        flyCamForward(scene->fly, fx, fy, fz);
        const float pos[3] = {
            scene->fly.eye[0] + fx * 8.0f,
            scene->fly.eye[1] + fy * 8.0f,
            scene->fly.eye[2] + fz * 8.0f,
        };
        if (kind == 6) {
            return sceneActorAdd(scene->actors, SceneActorKind::Empty, pos, "Empty");
        }
        if (scene->importedCount >= kLightCap) {
            return kSceneActorNone;
        }
        const uint32_t slot = scene->importedCount;
        LightGpuData lamp{};
        lamp.pos[0] = pos[0];
        lamp.pos[1] = pos[1];
        lamp.pos[2] = pos[2];
        lamp.color[0] = lamp.color[1] = lamp.color[2] = 1.0f;
        lamp.intensity = 8.0f;
        if (kind == 4) {
            lamp.type = kLightDirectional;
            lamp.direction[0] = -0.3f;
            lamp.direction[1] = -1.0f;
            lamp.direction[2] = -0.2f;
            lamp.intensity = 1.0f;
        } else {
            lamp.type = kLightPoint;
            lamp.radius = 8.0f;
        }
        scene->imported[slot] = lamp;
        scene->importedCount = slot + 1u;
        const char* name = kind == 4 ? "Directional Light" : "Point Light";
        const uint8_t actor = sceneActorAdd(scene->actors, SceneActorKind::Light, pos, name);
        if (actor < scene->actors.count && scene->actors.ent[actor].is_alive()) {
            scene->actors.ent[actor].set<LightLink>({slot, lamp.type});
        }
        return actor;
    }
    if (scene->instSrc == nullptr || scene->vertSrc == nullptr || scene->indexSrc == nullptr
        || scene->meshletSrc == nullptr || scene->propCount >= kPropCap) {
        return kSceneActorNone;
    }
    const bool cpuMesh = scene->vertSrc == scene->cpuVerts && scene->instSrc == scene->cpuInstances
        && scene->meshletSrc == scene->cpuMeshlets;
    const bool ownedMesh = !scene->ownedVert.empty() && scene->vertSrc == scene->ownedVert.data()
        && !scene->ownedInst.empty() && scene->instSrc == scene->ownedInst.data()
        && scene->meshletSrc == scene->ownedMesh.data();
    if (!cpuMesh && !ownedMesh) {
        return kSceneActorNone;
    }
    const uint32_t vertLimit = scene->vertCap > scene->vertCount ? scene->vertCap : scene->vertCount;
    const uint32_t indexLimit = scene->indexCap > scene->indexCount ? scene->indexCap : scene->indexCount;
    const uint32_t instLimit = scene->instanceCap > scene->instanceCount ? scene->instanceCap : scene->instanceCount;
    const uint32_t matLimit = scene->materialCap > scene->materialCount ? scene->materialCap : scene->materialCount;
    uint32_t vAdd = 24u;
    uint32_t iAdd = 36u;
    uint32_t tris = 12u;
    const char* stem = "Cube";
    if (kind == 1) {
        vAdd = 45u;
        iAdd = 192u;
        tris = 64u;
        stem = "Sphere";
    } else if (kind == 2) {
        stem = "Plane";
    } else if (kind == 3) {
        vAdd = 18u;
        iAdd = 96u;
        tris = 32u;
        stem = "Cylinder";
    }
    if (scene->vertCount + vAdd > vertLimit || scene->indexCount + iAdd > indexLimit
        || scene->instanceCount >= instLimit || scene->materialCount >= matLimit) {
        return kSceneActorNone;
    }
    const uint32_t matId = scene->materialCount;
    if (scene->materialSrc == scene->fallbackMat) {
        scene->ownedMat.assign(scene->fallbackMat, scene->fallbackMat + scene->materialCount);
        scene->ownedMat.reserve(matLimit);
        scene->materialSrc = scene->ownedMat.data();
    }
    if (scene->ownedMat.empty() || scene->materialSrc != scene->ownedMat.data()) {
        return kSceneActorNone;
    }
    scene->ownedMat.push_back({});
    scene->materialSrc = scene->ownedMat.data();
    MaterialGpuData& mat = scene->ownedMat.back();
    mat = {};
    mat.baseColor[0] = mat.baseColor[1] = mat.baseColor[2] = mat.baseColor[3] = 1.0f;
    mat.roughness = 0.08f;
    mat.metallic = 1.0f;
    scene->materialCount = static_cast<uint32_t>(scene->ownedMat.size());
    const uint32_t vBase = scene->vertCount;
    const uint32_t iBase = scene->indexCount;
    const uint32_t inst = scene->instanceCount;
    if (ownedMesh) {
        scene->ownedVert.resize(vBase + vAdd);
        scene->ownedIndex.resize(iBase + iAdd);
        if (scene->ownedInst.size() <= inst) {
            scene->ownedInst.resize(inst + 1u);
        }
        if (scene->ownedMesh.size() <= inst) {
            scene->ownedMesh.resize(inst + 1u);
        }
        scene->vertSrc = scene->ownedVert.data();
        scene->indexSrc = scene->ownedIndex.data();
        scene->instSrc = scene->ownedInst.data();
        scene->meshletSrc = scene->ownedMesh.data();
        GpuVertex* dstV = scene->ownedVert.data() + vBase;
        uint32_t* dstI = scene->ownedIndex.data() + iBase;
        if (kind == 1) {
            writeSphereMesh(dstV, dstI);
        } else if (kind == 2) {
            writePlane(dstV, dstI);
        } else if (kind == 3) {
            writeCylinder(dstV, dstI);
        } else {
            writeCube(dstV, dstI, 0.45f);
        }
    } else {
        GpuVertex* dstV = scene->cpuVerts + vBase;
        uint32_t* dstI = scene->cpuIndex + iBase;
        if (kind == 1) {
            writeSphereMesh(dstV, dstI);
        } else if (kind == 2) {
            writePlane(dstV, dstI);
        } else if (kind == 3) {
            writeCylinder(dstV, dstI);
        } else {
            writeCube(dstV, dstI, 0.45f);
        }
    }
    GpuInstance* table = ownedMesh ? scene->ownedInst.data() : scene->cpuInstances;
    GpuMeshlet* meshes = ownedMesh ? scene->ownedMesh.data() : scene->cpuMeshlets;
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = -1.0f;
    flyCamForward(scene->fly, fx, fy, fz);
    const float x = scene->fly.eye[0] + fx * 8.0f;
    const float y = scene->fly.eye[1] + fy * 8.0f;
    const float z = scene->fly.eye[2] + fz * 8.0f;
    instanceSet(table[inst], x, y, z, 0.45f * 1.7320508f, matId, inst);
    GpuMeshlet& mesh = meshes[inst];
    mesh = {};
    mesh.vertexOffset = vBase;
    mesh.indexOffset = iBase;
    mesh.vertexCount = vAdd;
    mesh.triangleCount = tris;
    mesh.coneCutoff = -1.0f;
    scene->vertCount = vBase + vAdd;
    scene->indexCount = iBase + iAdd;
    scene->instanceCount = inst + 1u;
    if (scene->verts.mapped != nullptr) {
        std::memcpy(static_cast<GpuVertex*>(scene->verts.mapped) + vBase, scene->vertSrc + vBase, sizeof(GpuVertex) * vAdd);
    }
    if (scene->indices.mapped != nullptr) {
        std::memcpy(static_cast<uint32_t*>(scene->indices.mapped) + iBase, scene->indexSrc + iBase, sizeof(uint32_t) * iAdd);
    }
    if (scene->instances.mapped != nullptr) {
        static_cast<GpuInstance*>(scene->instances.mapped)[inst] = table[inst];
    }
    if (scene->meshlets.mapped != nullptr) {
        static_cast<GpuMeshlet*>(scene->meshlets.mapped)[inst] = mesh;
    }
    if (scene->materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene->materialsBuf.mapped)[matId] = mat;
    }
    scene->editorDirty = static_cast<uint8_t>(scene->editorDirty | 2u);
    char name[24];
    std::snprintf(name, sizeof(name), "%s %u", stem, static_cast<unsigned>(scene->propCount + 1u));
    const uint8_t actor = linkProp(*scene, inst, name);
    if (actor != kSceneActorNone && scene->physics != nullptr) {
        physicsAddBox(scene->physics, inst, actor, table[inst], mesh, scene->vertSrc, scene->indexSrc);
    }
    return actor;
}

uint8_t sceneFrameSpawnCube(SceneFrame* scene) {
    return sceneFrameSpawn(scene, 0);
}

} // namespace burnhope
