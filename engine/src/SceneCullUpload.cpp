#include "audio/Audio.hpp"
#include "platform/PlatformUtil.hpp"
#include "gpu_scene/Physics.hpp"
#include "render/Weather.hpp"
#include "render/SceneFrameImpl.hpp"
#include "gpu_scene/InstanceCull.hpp"
#include "gpu_scene/PointShadow.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace burnhope {

uint8_t linkProp(SceneFrameImpl& s, uint32_t instance, const char* name) {
    if (s.propCount >= kPropCap || instance >= s.instanceCount || s.instSrc == nullptr) {
        return kSceneActorNone;
    }
    const GpuInstance& inst = s.instSrc[instance];
    const float pos[3] = {
        inst.sphere[3] > 0.02f ? inst.sphere[0] : inst.world[3],
        inst.sphere[3] > 0.02f ? inst.sphere[1] : inst.world[7],
        inst.sphere[3] > 0.02f ? inst.sphere[2] : inst.world[11],
    };
    const uint8_t actor = sceneActorAdd(s.actors, SceneActorKind::Prop, pos, name);
    if (actor == kSceneActorNone) {
        return kSceneActorNone;
    }
    SceneFrameImpl::PropLink& link = s.props[s.propCount];
    link.instance = instance;
    link.material = inst.materialId;
    link.actor = actor;
    link.radius = inst.sphere[3];
    link.pose = sceneActorXform(s.actors, actor);
    if (actor < s.actors.count && s.actors.ent[actor].is_alive()) {
        const float half = link.radius > 0.02f ? link.radius / 1.7320508f : 0.5f;
        ActorBounds box{};
        box.half[0] = box.half[1] = box.half[2] = half;
        s.actors.ent[actor].set<ActorBounds>(box);
    }
    s.propCount += 1;
    if (actor < s.actors.count && s.actors.ent[actor].is_alive()) {
        const MaterialGpuData* src = materialAt(s, inst.materialId);
        if (src != nullptr) {
            Material pbr{};
            pbr.base[0] = src->baseColor[0];
            pbr.base[1] = src->baseColor[1];
            pbr.base[2] = src->baseColor[2];
            pbr.base[3] = src->baseColor[3];
            pbr.roughness = src->roughness;
            pbr.metallic = src->metallic;
            pbr.emissive = src->emissiveIntensity;
            s.actors.ent[actor].set<Material>(pbr);
        }
    }
    return actor;
}

void bakeAudio(SceneFrameImpl& scene) {
    if (scene.audio == nullptr || scene.vertSrc == nullptr || scene.indexSrc == nullptr || scene.instSrc == nullptr || scene.meshletSrc == nullptr) {
        return;
    }
    constexpr uint32_t kCap = 200000;
    const uint32_t meshCount = scene.meshletSrc == scene.cpuMeshlets
        ? kSceneInstances
        : static_cast<uint32_t>(scene.ownedMesh.size());
    uint32_t total = 0;
    for (uint32_t i = 0; i < scene.instanceCount; ++i) {
        const GpuInstance& inst = scene.instSrc[i];
        if (inst.meshId >= meshCount) {
            continue;
        }
        total += scene.meshletSrc[inst.meshId].triangleCount;
    }
    const uint32_t step = total > kCap ? (total + kCap - 1u) / kCap : 1u;
    std::vector<float> xyz;
    std::vector<int32_t> idx;
    std::vector<int32_t> mat;
    xyz.reserve(std::min(total, kCap) * 3u);
    idx.reserve(std::min(total, kCap) * 3u);
    mat.reserve(std::min(total, kCap));
    uint32_t seen = 0;
    for (uint32_t i = 0; i < scene.instanceCount; ++i) {
        const GpuInstance& inst = scene.instSrc[i];
        if (inst.meshId >= meshCount) {
            continue;
        }
        const GpuMeshlet& mesh = scene.meshletSrc[inst.meshId];
        if (mesh.vertexCount == 0 || mesh.triangleCount == 0) {
            continue;
        }
        if (mesh.vertexOffset + mesh.vertexCount > scene.vertCount || mesh.indexOffset + mesh.triangleCount * 3u > scene.indexCount) {
            continue;
        }
        for (uint32_t t = 0; t < mesh.triangleCount; ++t) {
            const uint32_t keep = seen;
            seen += 1u;
            if ((keep % step) != 0u) {
                continue;
            }
            const uint32_t at = mesh.indexOffset + t * 3u;
            const uint32_t i0 = scene.indexSrc[at];
            const uint32_t i1 = scene.indexSrc[at + 1u];
            const uint32_t i2 = scene.indexSrc[at + 2u];
            if (i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount) {
                continue;
            }
            const GpuVertex& a = scene.vertSrc[mesh.vertexOffset + i0];
            const GpuVertex& b = scene.vertSrc[mesh.vertexOffset + i1];
            const GpuVertex& c = scene.vertSrc[mesh.vertexOffset + i2];
            float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
            float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
            float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
            x0 = inst.world[0] * a.px + inst.world[1] * a.py + inst.world[2] * a.pz + inst.world[3];
            y0 = inst.world[4] * a.px + inst.world[5] * a.py + inst.world[6] * a.pz + inst.world[7];
            z0 = inst.world[8] * a.px + inst.world[9] * a.py + inst.world[10] * a.pz + inst.world[11];
            x1 = inst.world[0] * b.px + inst.world[1] * b.py + inst.world[2] * b.pz + inst.world[3];
            y1 = inst.world[4] * b.px + inst.world[5] * b.py + inst.world[6] * b.pz + inst.world[7];
            z1 = inst.world[8] * b.px + inst.world[9] * b.py + inst.world[10] * b.pz + inst.world[11];
            x2 = inst.world[0] * c.px + inst.world[1] * c.py + inst.world[2] * c.pz + inst.world[3];
            y2 = inst.world[4] * c.px + inst.world[5] * c.py + inst.world[6] * c.pz + inst.world[7];
            z2 = inst.world[8] * c.px + inst.world[9] * c.py + inst.world[10] * c.pz + inst.world[11];
            const float e1x = x1 - x0;
            const float e1y = y1 - y0;
            const float e1z = z1 - z0;
            const float e2x = x2 - x0;
            const float e2y = y2 - y0;
            const float e2z = z2 - z0;
            const float nx = e1y * e2z - e1z * e2y;
            const float ny = e1z * e2x - e1x * e2z;
            const float nz = e1x * e2y - e1y * e2x;
            const float area2 = nx * nx + ny * ny + nz * nz;
            if (area2 < 1.0e-8f) {
                continue;
            }
            const int32_t base = static_cast<int32_t>(xyz.size() / 3u);
            xyz.push_back(x0);
            xyz.push_back(y0);
            xyz.push_back(z0);
            xyz.push_back(x1);
            xyz.push_back(y1);
            xyz.push_back(z1);
            xyz.push_back(x2);
            xyz.push_back(y2);
            xyz.push_back(z2);
            idx.push_back(base);
            idx.push_back(base + 1);
            idx.push_back(base + 2);
            const float len = std::sqrt(area2);
            mat.push_back(ny > 0.65f * len ? 1 : 0);
            if (mat.size() >= kCap) {
                break;
            }
        }
        if (mat.size() >= kCap) {
            break;
        }
    }
    AudioMesh mesh{};
    mesh.xyz = xyz.data();
    mesh.vertexCount = static_cast<uint32_t>(xyz.size() / 3u);
    mesh.indices = idx.data();
    mesh.materialIndex = mat.data();
    mesh.triangleCount = static_cast<uint32_t>(mat.size());
    audioSetMesh(scene.audio, mesh);
    spdlog::info("audio mesh {} triangles", mesh.triangleCount);
}

void uploadStatic(SceneFrameImpl& s) {
    if (s.vertSrc == nullptr || s.verts.mapped == nullptr) {
        return;
    }
    std::memcpy(s.verts.mapped, s.vertSrc, sizeof(GpuVertex) * s.vertCount);
    std::memcpy(s.indices.mapped, s.indexSrc, sizeof(uint32_t) * s.indexCount);
    std::memcpy(s.instances.mapped, s.instSrc, sizeof(GpuInstance) * s.instanceCount);
    std::memcpy(s.meshlets.mapped, s.meshletSrc, sizeof(GpuMeshlet) * s.instanceCount);
    if (s.materialSrc != nullptr && s.materialCount > 0) {
        std::memcpy(s.materialsBuf.mapped, s.materialSrc, sizeof(MaterialGpuData) * s.materialCount);
        auto* mats = static_cast<MaterialGpuData*>(s.materialsBuf.mapped);
        for (uint32_t i = 0; i < s.materialCount; ++i) {
            if (s.texNames != nullptr) {
                const uint32_t tex = mats[i].albedoTexIndex;
                if (tex > 0u && tex < s.texNameCount) {
                    const char* file = s.texNames[tex].file;
                    if (nameHas(file, "leaf") || nameHas(file, "foliage") || nameHas(file, "grass") || nameHas(file, "hedge")
                        || nameHas(file, "fern") || nameHas(file, "azalea") || nameHas(file, "linden")) {
                        mats[i].flags |= 32u;
                    }
                }
            }
        }
    }
}

uint32_t cullLod(SceneFrameImpl& s, const CameraCull& cam) {
    return cullInstances(cam, s.instSrc, s.instanceCount, s.visibleCpu.data(), s.visibleCap, true);
}

uint32_t cullPoint(const SceneFrameImpl& s, float lx, float ly, float lz, float radius, uint32_t* out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < s.instanceCount && n < s.visibleCap; ++i) {
        const GpuInstance& inst = s.instSrc[i];
        if (inst.pad[0] != 0) {
            continue;
        }
        const float dx = inst.sphere[0] - lx;
        const float dy = inst.sphere[1] - ly;
        const float dz = inst.sphere[2] - lz;
        const float lim = radius + inst.sphere[3];
        if (dx * dx + dy * dy + dz * dz > lim * lim) {
            continue;
        }
        out[n++] = i;
    }
    return n;
}

uint32_t cullFace(const SceneFrameImpl& s, const Mat4& view, uint32_t* out) {
    uint32_t n = 0;
    const float* m = view.m;
    for (uint32_t i = 0; i < s.instanceCount && n < s.visibleCap; ++i) {
        const GpuInstance& inst = s.instSrc[i];
        if (inst.pad[0] != 0) {
            continue;
        }
        const float x = inst.sphere[0];
        const float y = inst.sphere[1];
        const float z = inst.sphere[2];
        const float r = inst.sphere[3];
        const float vx = m[0] * x + m[4] * y + m[8] * z + m[12];
        const float vy = m[1] * x + m[5] * y + m[9] * z + m[13];
        const float vz = m[2] * x + m[6] * y + m[10] * z + m[14];
        if (vz > r) {
            continue;
        }
        const float lim = -vz + r;
        if (std::fabs(vx) > lim || std::fabs(vy) > lim) {
            continue;
        }
        out[n++] = i;
    }
    return n;
}

bool ensureOwnedInstances(SceneFrameImpl& s) {
    if (s.instSrc == nullptr || s.instanceCount == 0) {
        return false;
    }
    if (s.ownedInst.size() == s.instanceCount && s.instSrc == s.ownedInst.data()) {
        return true;
    }
    s.ownedInst.assign(s.instSrc, s.instSrc + s.instanceCount);
    s.instSrc = s.ownedInst.data();
    return true;
}
} // namespace burnhope
