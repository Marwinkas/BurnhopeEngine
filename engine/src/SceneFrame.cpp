#include "audio/Audio.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/Particles.hpp"
#include "render/Physics.hpp"
#include "render/SceneFrame.hpp"
#include "render/Weather.hpp"
#include "render/SceneFrameImpl.hpp"

#include "debug/DebugStats.hpp"
#include "gpu_scene/Camera.hpp"
#include "gpu_scene/InstanceCull.hpp"
#include "gpu_scene/InstanceData.hpp"
#include "gfx/Decal.hpp"
#include "gfx/Portal.hpp"
#include "gfx/Probe.hpp"
#include "gpu_scene/MeshletGpu.hpp"
#include "gpu_scene/LightMath.hpp"
#include "gpu_scene/PointShadow.hpp"
#include "gpu_scene/SunShadow.hpp"
#include "image/ImageFile.hpp"
#include "gfx/Material.hpp"
#include "render/Cull.hpp"
#include "render/Cube.hpp"
#include "render/Shade.hpp"
#include "render/Hiz.hpp"
#include "render/Radiance.hpp"
#include "render/Gizmo.hpp"
#include "render/Tonemap.hpp"
#include "render/Visbuffer.hpp"
#include "debug/CpuScope.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"
#include "scene/TestScene.hpp"
#include "asset/SceneBlob.hpp"
#include "gpu_scene/InstanceData.hpp"
#include "io/File.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace burnhope {
namespace {

void placeActors(SceneFrameImpl& scene) {
    sceneActorsClear(scene.actors);
    const float post[3] = {scene.fly.eye[0], scene.fly.eye[1] + 2.0f, scene.fly.eye[2] - 6.0f};
    sceneActorAdd(scene.actors, SceneActorKind::Post, post, "Post");
    const float climate[3] = {post[0] + 3.0f, post[1], post[2]};
    const uint8_t weather = sceneActorAdd(scene.actors, SceneActorKind::Weather, climate, "Weather");
    scene.actors.selected = weather;
    if (weather < scene.actors.count) {
        Xform pose = sceneActorXform(scene.actors, weather);
        pose.euler[0] = 35.0f;
        pose.euler[1] = 30.0f;
        sceneActorWriteXform(scene.actors, weather, pose);
        scene.weatherEnt = scene.actors.ent[weather];
        scene.weatherEnt.set<Weather>(Weather{});
        sceneActorFitTool(scene.actors);
    }
}

} // namespace
namespace {

bool nameHas(const char* text, const char* needle);

void kelvinRgb(float kelvin, float& r, float& g, float& b) {
    float t = kelvin;
    if (t < 1500.0f) {
        t = 1500.0f;
    }
    if (t > 12000.0f) {
        t = 12000.0f;
    }
    t /= 100.0f;
    auto sat = [](float v) {
        if (v < 0.0f) {
            return 0.0f;
        }
        if (v > 255.0f) {
            return 255.0f;
        }
        return v;
    };
    if (t <= 66.0f) {
        r = 255.0f;
        g = sat(99.4708025861f * std::log(t) - 161.1195681661f);
        b = t <= 19.0f ? 0.0f : sat(138.5177312231f * std::log(t - 10.0f) - 305.0447927307f);
    } else {
        r = sat(329.698727446f * std::pow(t - 60.0f, -0.1332047592f));
        g = sat(288.1221695283f * std::pow(t - 60.0f, -0.0755148492f));
        b = 255.0f;
    }
    r /= 255.0f;
    g /= 255.0f;
    b /= 255.0f;
}

void pushBlob(SceneFrameImpl& s, const MeshletBlob& blob, float x, float y, float z) {
    if (s.instanceCount >= kSceneInstances || s.vertCount + blob.vertexCount > kSceneVerts) {
        return;
    }
    const uint32_t base = s.vertCount;
    const uint32_t indexAt = s.indexCount;
    for (uint32_t i = 0; i < blob.vertexCount; ++i) {
        GpuVertex& dst = s.cpuVerts[s.vertCount];
        dst = {};
        dst.px = blob.vertices[i].pos[0];
        dst.py = blob.vertices[i].pos[1];
        dst.pz = blob.vertices[i].pos[2];
        dst.ny = 1.0f;
        s.vertCount += 1;
    }
    uint32_t tris = 0;
    for (uint32_t t = 0; t < blob.triangleCount && s.indexCount + 3 <= kSceneTris * 3; ++t) {
        s.cpuIndex[s.indexCount + 0] = blob.indices[t * 3 + 0];
        s.cpuIndex[s.indexCount + 1] = blob.indices[t * 3 + 1];
        s.cpuIndex[s.indexCount + 2] = blob.indices[t * 3 + 2];
        s.indexCount += 3;
        tris += 1;
    }
    GpuMeshlet& meshlet = s.cpuMeshlets[s.instanceCount];
    meshlet.vertexOffset = base;
    meshlet.indexOffset = indexAt;
    meshlet.vertexCount = blob.vertexCount;
    meshlet.triangleCount = tris;
    const float radius = blob.meshlets[0].radius > 0.0f ? blob.meshlets[0].radius : 1.0f;
    instanceSet(s.cpuInstances[s.instanceCount], x, y, z, radius, blob.materialId, s.instanceCount);
    s.instanceCount += 1;
}

MaterialGpuData* materialAt(SceneFrameImpl& s, uint32_t id) {
    if (s.materialSrc == nullptr || id >= s.materialCount) {
        return nullptr;
    }
    if (!s.ownedMat.empty() && s.materialSrc == s.ownedMat.data() && id < s.ownedMat.size()) {
        return &s.ownedMat[id];
    }
    if (s.materialSrc == s.fallbackMat && id < 4u) {
        return &s.fallbackMat[id];
    }
    return nullptr;
}

void materialOwnFromView(SceneFrameImpl& s, const SceneBlobView& view) {
    const uint32_t count = view.info != nullptr ? view.info->materialCount : 0u;
    s.ownedMat.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* src = nullptr;
        if (view.materialRaw != nullptr && view.materialStride > 0u) {
            src = view.materialRaw + static_cast<size_t>(i) * view.materialStride;
        }
        materialAdoptRecord(s.ownedMat[i], src, view.materialStride);
    }
    s.materialSrc = count > 0u ? s.ownedMat.data() : nullptr;
    s.materialCount = count;
}

void armEditorRoom(SceneFrameImpl& s) {
    const uint32_t vRoom = kEditorSpare * 24u;
    const uint32_t iRoom = kEditorSpare * 36u;
    if (!s.ownedVert.empty() && s.vertSrc == s.ownedVert.data()) {
        s.ownedVert.reserve(s.ownedVert.size() + vRoom);
        s.ownedIndex.reserve(s.ownedIndex.size() + iRoom);
        s.ownedInst.reserve(s.ownedInst.size() + kEditorSpare);
        s.ownedMesh.reserve(s.ownedMesh.size() + kEditorSpare);
        s.ownedMat.reserve(s.ownedMat.size() + kEditorSpare);
        s.vertSrc = s.ownedVert.data();
        s.indexSrc = s.ownedIndex.data();
        s.instSrc = s.ownedInst.data();
        s.meshletSrc = s.ownedMesh.data();
        s.materialSrc = s.ownedMat.data();
        s.vertCap = s.vertCount + vRoom;
        s.indexCap = s.indexCount + iRoom;
        s.instanceCap = s.instanceCount + kEditorSpare;
        s.materialCap = s.materialCount + kEditorSpare;
    } else if (s.vertSrc == s.cpuVerts) {
        s.vertCap = kSceneVerts;
        s.indexCap = kSceneTris * 3u;
        s.instanceCap = kSceneInstances;
        s.materialCap = s.materialCount + kEditorSpare;
    }
    if (s.instanceCap > s.visibleCap) {
        s.visibleCpu.resize(s.instanceCap, 0u);
        s.visibleCap = s.instanceCap;
    }
}

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

bool fileReadable(const char* path) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

bool endsWith(const char* text, const char* suffix) {
    if (text == nullptr || suffix == nullptr) {
        return false;
    }
    const size_t n = std::strlen(text);
    const size_t m = std::strlen(suffix);
    return n >= m && std::strcmp(text + (n - m), suffix) == 0;
}

void resolveTexturePath(char* path, size_t cap, const char* root, const char* name) {
    std::snprintf(path, cap, "%s/%s", root, name);
    if (fileReadable(path) || name == nullptr || name[0] == '\0') {
        return;
    }
    const char* cut[] = {".d", ".dd", ".pn", ".jp"};
    const char* full[] = {".dds", ".dds", ".png", ".jpg"};
    for (uint32_t i = 0; i < 4; ++i) {
        if (!endsWith(name, cut[i])) {
            continue;
        }
        const size_t n = std::strlen(name);
        const size_t c = std::strlen(cut[i]);
        std::string stem(name, n - c);
        stem += full[i];
        std::snprintf(path, cap, "%s/%s", root, stem.c_str());
        if (fileReadable(path)) {
            return;
        }
    }
    std::error_code ec;
    const std::filesystem::path dir(root);
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    const size_t n = std::strlen(name);
    std::string only;
    uint32_t hits = 0;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string file = it->path().filename().string();
        if (file.empty() || file[0] == '.') {
            continue;
        }
        if (file.compare(0, n, name) != 0) {
            continue;
        }
        only = file;
        hits += 1;
        if (hits > 1) {
            break;
        }
    }
    if (hits == 1 && !only.empty()) {
        std::snprintf(path, cap, "%s/%s", root, only.c_str());
    }
}

bool nameHas(const char* text, const char* needle) {
    if (text == nullptr || needle == nullptr) {
        return false;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        const char* a = p;
        const char* b = needle;
        while (*a != '\0' && *b != '\0'
            && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

bool uploadDummy(GpuImage& img, Device& device, const char* name) {
    uint8_t px[4] = {0xff, 0xff, 0xff, 0xff};
    if (nameHas(name, "normal") || nameHas(name, "nrm")) {
        px[0] = 0x80;
        px[1] = 0x80;
        px[2] = 0xff;
        px[3] = 0xff;
    } else if (nameHas(name, "orm") || nameHas(name, "spec") || nameHas(name, "rough") || nameHas(name, "metal")) {
        px[0] = 0xff;
        px[1] = 0x80;
        px[2] = 0x00;
        px[3] = 0xff;
    }
    return imageUploadUncompressed(img, device, px, 1, 1);
}

void ddsCachePath(char* out, size_t cap, const char* resolved) {
    std::snprintf(out, cap, "cache/textures/");
    size_t n = std::strlen(out);
    if (resolved != nullptr) {
        for (const char* p = resolved; *p != '\0' && n + 6 < cap; ++p) {
            const char c = (*p == '/' || *p == ' ') ? '_' : *p;
            out[n++] = c;
        }
    }
    if (n + 5 < cap) {
        out[n++] = '.';
        out[n++] = 'd';
        out[n++] = 'd';
        out[n++] = 's';
    }
    out[n] = '\0';
}

bool imageName(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".bmp"
        || ext == ".PNG" || ext == ".JPG" || ext == ".JPEG" || ext == ".DDS" || ext == ".TGA";
}

bool skipTextureDir(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name == "cache" || name == "build" || name == "build-core" || name == ".git" || name == "shaders"
        || name == ".cache";
}

void indexTextureDir(const std::filesystem::path& dir, std::unordered_map<std::string, std::vector<std::string>>& index,
    int depth) {
    if (depth < 0 || skipTextureDir(dir)) {
        return;
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& child = it->path();
        std::error_code kind;
        if (it->is_directory(kind)) {
            indexTextureDir(child, index, depth - 1);
            continue;
        }
        if (kind || !it->is_regular_file(kind) || !imageName(child)) {
            continue;
        }
        auto& hits = index[child.filename().string()];
        if (hits.size() < 8u) {
            hits.push_back(child.string());
        }
    }
}

const char* packHint(const char* name) {
    if (name == nullptr || name[0] == '\0' || name[1] != '/') {
        return nullptr;
    }
    switch (name[0]) {
    case 'b':
        return "curtain";
    case 'c':
        return "ivy";
    case 'd':
        return "tree";
    case 'e':
        return "candle";
    default:
        return "sponza";
    }
}

const std::string* pickTexture(const std::vector<std::string>& hits, const char* hint) {
    const std::string* fallback = nullptr;
    for (const std::string& path : hits) {
        if (hint != nullptr && nameHas(path.c_str(), hint)) {
            return &path;
        }
        if (fallback == nullptr && nameHas(path.c_str(), "texture")) {
            fallback = &path;
        }
    }
    if (fallback != nullptr) {
        return fallback;
    }
    return hits.empty() ? nullptr : &hits.front();
}

void indexSceneTextures(std::unordered_map<std::string, std::vector<std::string>>& index) {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        indexTextureDir(cwd, index, 4);
    }
    char exe[512]{};
    if (!platformExePath(exe, sizeof(exe))) {
        return;
    }
    const std::filesystem::path project = std::filesystem::path(exe).parent_path().parent_path();
    if (project.empty() || project == cwd || project == project.root_path()) {
        return;
    }
    indexTextureDir(project, index, 4);
}

const char* textureLeaf(const char* name) {
    if (name != nullptr && name[0] != '\0' && name[1] == '/') {
        return name + 2;
    }
    return name;
}

bool loadTextures(SceneFrameImpl& s, Device& device) {
    uint32_t count = s.texNameCount == 0 ? 1u : s.texNameCount;
    if (count > 768u) {
        count = 768u;
    }
    s.textures.assign(count, GpuImage{});
    s.textureSlots = count;
    const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    if (!imageUploadUncompressed(s.textures[0], device, white, 1, 1)) {
        return false;
    }
    std::unordered_map<std::string, std::vector<std::string>> indexed;
    indexSceneTextures(indexed);
    for (uint32_t i = 1; i < count; ++i) {
        char path[512];
        const char* name = s.texNames[i].file;
        const char* leaf = textureLeaf(name);
        path[0] = '\0';
        auto tryRoot = [&](const char* root, const char* file) {
            if (root == nullptr || root[0] == '\0' || file == nullptr || file[0] == '\0') {
                return false;
            }
            resolveTexturePath(path, sizeof(path), root, file);
            return fileReadable(path);
        };
        char beside[512]{};
        char textures[540]{};
        bool found = false;
        if (s.bhopPath[0] != '\0' && pathParent(s.bhopPath, beside, sizeof(beside))) {
            found = tryRoot(beside, name);
            if (!found) {
                std::snprintf(textures, sizeof(textures), "%s/textures", beside);
                found = tryRoot(textures, leaf);
            }
            if (!found) {
                found = tryRoot(beside, leaf);
            }
        }
        if (!found) {
            found = tryRoot("assets/textures", leaf);
        }
        if (!found && leaf != nullptr) {
            const auto hit = indexed.find(leaf);
            if (hit != indexed.end()) {
                const std::string* picked = pickTexture(hit->second, packHint(name));
                if (picked != nullptr && picked->size() + 1 < sizeof(path)) {
                    std::memcpy(path, picked->c_str(), picked->size() + 1);
                    found = fileReadable(path);
                }
            }
        }
        if (!found) {
            uploadDummy(s.textures[i], device, name);
            continue;
        }
        char cache[512];
        ddsCachePath(cache, sizeof(cache), path);
        bool dataMap = false;
        if (s.materialSrc != nullptr) {
            for (uint32_t m = 0; m < s.materialCount; ++m) {
                const MaterialGpuData& mat = s.materialSrc[m];
                if (mat.normalTexIndex == i || mat.ormTexIndex == i || mat.pad[0] == i || mat.pad[2] == i || mat.pad[3] == i) {
                    dataMap = true;
                    break;
                }
            }
        }
        const bool precise = dataMap || nameHas(name, "normal") || nameHas(name, "nrm") || nameHas(name, "orm")
            || nameHas(name, "rough") || nameHas(name, "metal") || nameHas(name, "spec");
        const bool srgb = !precise;
        bool loaded = imageLoadDds(s.textures[i], device, cache, srgb);
        if (loaded && precise && s.textures[i].format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) {
            gpuImageDestroy(s.textures[i], device);
            loaded = false;
        }
        if (!loaded) {
            loaded = imageLoadDds(s.textures[i], device, path, srgb);
        }
        if (!loaded) {
            ImageCpu cpu{};
            if (imageLoadFile(cpu, path)) {
                if (cpu.hdr != 0 && cpu.rgb != nullptr && srgb) {
                    loaded = gpuImageUploadHdr(s.textures[i], device, cpu.rgb, cpu.width, cpu.height);
                } else if (cpu.rgba != nullptr) {
                    loaded = gpuImageUploadPbr(
                        s.textures[i], device, cpu.rgba, cpu.width, cpu.height, srgb ? PbrTex::Color : PbrTex::Data, cache);
                }
            }
            imageCpuFree(cpu);
        }
        if (!loaded && !uploadDummy(s.textures[i], device, name)) {
            uploadDummy(s.textures[i], device, "");
        }
    }
    return true;
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

bool heapImage(
    DescriptorHeaps& heaps,
    Device& device,
    SceneFrameImpl& s,
    uint32_t slot,
    VkDescriptorType type,
    const GpuImage& img,
    VkImageLayout layout,
    VkImageAspectFlags aspect) {
    const GpuImage* use = &img;
    if (use->image == VK_NULL_HANDLE || use->view == VK_NULL_HANDLE) {
        use = s.textures.empty() ? &img : &s.textures.front();
    }
    if (use->image == VK_NULL_HANDLE || use->view == VK_NULL_HANDLE || !imageSlotLive(slot)) {
        return false;
    }
    const uint32_t mips = type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? use->mips : 1u;
    return heapWriteImage(heaps, device, slot, type, *use, layout, aspect, mips);
}

bool writeKnob(DescriptorHeaps& heaps, Device& device, const GpuBuffer& post) {
    const uint32_t slot[] = {
        static_cast<uint32_t>(HeapBuf::Flags),
        static_cast<uint32_t>(HeapBuf::Gtao),
        static_cast<uint32_t>(HeapBuf::Ssr),
        static_cast<uint32_t>(HeapBuf::Ssrc),
        static_cast<uint32_t>(HeapBuf::RcParams),
        static_cast<uint32_t>(HeapBuf::Shade),
        static_cast<uint32_t>(HeapBuf::Sliders),
        static_cast<uint32_t>(HeapBuf::Bloom),
    };
    for (uint32_t i = 0; i < 8u; ++i) {
        const VkDeviceAddress at = post.address + 64ull * i;
        if (!heapWriteBufferFlight(heaps, device, 0, slot[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, at, 64)
            || !heapWriteBufferFlight(heaps, device, 1, slot[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, at + sizeof(FramePost), 64)) {
            return false;
        }
    }
    const VkDeviceAddress gradeAt = post.address + sizeof(FramePost) * 2;
    if (!heapWriteBufferFlight(heaps, device, 0, kGradeSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, gradeAt, sizeof(GradeBlock))
        || !heapWriteBufferFlight(heaps, device, 1, kGradeSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, gradeAt + sizeof(GradeBlock), sizeof(GradeBlock))) {
        return false;
    }
    return true;
}

bool bindHeap(SceneFrameImpl& s, Device& device, DescriptorHeaps& heaps) {
    const bool buffers = writeKnob(heaps, device, s.postBuf);
    struct StorageBind {
        HeapBuf slot;
        GpuBuffer* buf;
    };
    const StorageBind table[] = {
        {HeapBuf::Verts, &s.verts},
        {HeapBuf::Indices, &s.indices},
        {HeapBuf::Meshlets, &s.meshlets},
        {HeapBuf::Instances, &s.instances},
        {HeapBuf::Visible, &s.visible},
        {HeapBuf::Materials, &s.materialsBuf},
    };
    bool stored = buffers
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Frame), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.frameBuf.address, sizeof(FrameView))
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Frame), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.frameBuf.address + sizeof(FrameView), sizeof(FrameView))
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Sun), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.sunBuf.address, sizeof(FrameSun))
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Sun), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.sunBuf.address + sizeof(FrameSun), sizeof(FrameSun))
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Atm), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.atmBuf.address, sizeof(AtmosphereParams))
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Atm), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.atmBuf.address + sizeof(AtmosphereParams), sizeof(AtmosphereParams));
    for (const StorageBind& row : table) {
        stored = stored && heapWriteStorage(heaps, device, row.slot, *row.buf);
    }
    const VkDeviceSize list = alignUp(s.visibleCap) * sizeof(uint32_t);
    bool shadowLists = s.shadowVisible.address != 0;
    for (uint32_t flight = 0; shadowLists && flight < 2u; ++flight) {
        const VkDeviceAddress base = s.shadowVisible.address + list * 3u * flight;
        shadowLists = heapWriteBufferFlight(
            heaps, device, flight, static_cast<uint32_t>(HeapBuf::ShadowAll),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, base, list * 3u);
        for (uint32_t c = 0; shadowLists && c < 3u; ++c) {
            shadowLists = heapWriteBufferFlight(
                heaps, device, flight, shadowListSlot(c),
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, base + list * c, list);
        }
    }
    bool extra = stored && shadowLists;
    if (extra && s.pointVisible.address != 0) {
        extra = heapWriteStorage(heaps, device, static_cast<uint32_t>(HeapBuf::PointVisible), s.pointVisible.address, list * 3u);
    }
    for (uint32_t i = 0; extra && i < 6; ++i) {
        extra = heapWriteStorage(heaps, device, cubeListSlot(i), s.cubeVisible.address + list * i, list);
    }
    extra = extra && heapWriteStorage(heaps, device, static_cast<uint32_t>(HeapBuf::CubeVisible), s.cubeVisible.address, list * 6);
    const bool images =
        heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Vis), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.vis.targets.vis.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Depth), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.vis.targets.depth.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrAStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.hdrA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrBSampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.hdrA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImageFlight(heaps, device, 0, static_cast<uint32_t>(HeapImg::Shadow), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shadowDepth[0].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1)
        && heapWriteImageFlight(heaps, device, 1, static_cast<uint32_t>(HeapImg::Shadow), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shadowDepth[1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ShadowB), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shadowDepth[1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    const bool cascades = images;
    VkSamplerCreateInfo sampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    const bool pictures =
        heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrASampled), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.hdrA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HdrBStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.hdrB.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::AoStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.ao.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Ao), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.ao.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Contact), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.contact.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ContactSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.shade.contact.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Reflect), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.reflect.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ReflectHist0), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.reflectHist[0].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::ReflectHist1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.reflectHist[1].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Surf0), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.surf0.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Surf1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.shade.surf1.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyTrans), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.transmittance.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyTransSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.transmittance.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyMs), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.multiScatter.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyView), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.skyView.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::SkyViewSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.skyView.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Cloud), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.cloud.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CloudSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.cloud.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CloudShadow), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.sky.shadow.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CloudShadowSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.sky.shadow.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::BloomStorage), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.tone.bloom.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Bloom), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.tone.bloom.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::PointShadow), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.pointDepth.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Cube), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.cubeColor.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::CubeFill), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.cubeColor.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMax), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.hizMax[0].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMaxSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.hizMax[0].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMax1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s.hizMax[1].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::HiZMaxSample1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s.hizMax[1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VkSamplerCreateInfo wrap = sampler;
    wrap.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    wrap.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    wrap.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    wrap.maxLod = 12.0f;
    if (device.caps.samplerAnisotropy) {
        wrap.anisotropyEnable = VK_TRUE;
        wrap.maxAnisotropy = 16.0f;
    }
    const bool lamps =
        s.lightBuf.address != 0
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::Lights), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s.lightBuf.address, sizeof(LightGpuData) * kLightCap)
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::Lights), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s.lightBuf.address + sizeof(LightGpuData) * kLightCap, sizeof(LightGpuData) * kLightCap)
        && heapWriteStorage(heaps, device, HeapBuf::Clusters, s.cellBuf)
        && heapWriteStorage(heaps, device, HeapBuf::ClusterIndex, s.lightIndex)
        && heapWriteStorage(heaps, device, HeapBuf::Decals, s.decalBuf)
        && heapWriteStorage(heaps, device, HeapBuf::Probes, s.probeBuf)
        && heapWriteStorage(heaps, device, HeapBuf::CloudNoise, s.sky.noise)
        && heapWriteStorage(heaps, device, HeapBuf::Sectors, s.sectorBuf)
        && heapWriteStorage(heaps, device, HeapBuf::ShadowPages, s.shadowPageBuf)
        && heapWriteStorage(heaps, device, HeapBuf::TexPages, s.texPageBuf)
        && heapWriteStorage(heaps, device, HeapBuf::TexFeedback, s.texFeedbackBuf)
        && heapWriteStorage(heaps, device, HeapBuf::Candidates, s.candidates)
        && heapWriteStorage(heaps, device, HeapBuf::Indirect, s.indirect)
        && s.shadowIndirect.address != 0
        && heapWriteStorage(heaps, device, HeapBuf::ShadowIndirect, s.shadowIndirect);
    VkSamplerCreateInfo nearest = sampler;
    nearest.magFilter = VK_FILTER_NEAREST;
    nearest.minFilter = VK_FILTER_NEAREST;
    nearest.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (!(extra && cascades && pictures && lamps
        && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Nearest), nearest)
        && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Linear), sampler)
        && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Wrap), wrap))) {
        return false;
    }
    bool volumes = s.voxel.atoms.address != 0 && s.voxel.clip.address != 0
        && heapWriteStorage(heaps, device, HeapBuf::Voxel, s.voxel.atoms)
        && heapWriteBufferFlight(heaps, device, 0, static_cast<uint32_t>(HeapBuf::VxClip), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.voxel.clip.address, 256)
        && heapWriteBufferFlight(heaps, device, 1, static_cast<uint32_t>(HeapBuf::VxClip), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s.voxel.clip.address + 256, 256);
    for (uint32_t flight = 0; volumes && flight < 2u; ++flight) {
        for (uint32_t cascade = 0; volumes && cascade < 3u; ++cascade) {
            const GpuImage& pic = s.voxel.volume[flight][cascade].image;
            volumes = heapImage(heaps, device, s, voxelImageSlot(false, flight, cascade), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, pic, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
                && heapImage(heaps, device, s, voxelImageSlot(true, flight, cascade), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, pic, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }
    if (!volumes) {
        return false;
    }
    for (uint32_t i = 0; i < s.textureSlots; ++i) {
        const GpuImage& tex = s.textures[i].image != VK_NULL_HANDLE ? s.textures[i] : s.textures[0];
        if (!heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Textures) + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return false;
        }
    }
    return restirBind(s.restir, device, heaps);
}

uint32_t uploadLights(SceneFrameImpl& s, uint32_t flight) {
    const LightGpuData* src = s.importedCount > 0 ? s.imported : s.world.lights;
    uint32_t count = s.importedCount > 0 ? s.importedCount : s.world.lightCount;
    if (count > kLightCap) {
        count = kLightCap;
    }
    if (s.lightBuf.mapped == nullptr) {
        return 0;
    }
    auto* dst = static_cast<uint8_t*>(s.lightBuf.mapped) + sizeof(LightGpuData) * kLightCap * flight;
    std::memcpy(dst, src, sizeof(LightGpuData) * count);
    if (s.handOn && count < kLightCap) {
        std::memcpy(dst + sizeof(LightGpuData) * count, &s.handLamp, sizeof(s.handLamp));
        count += 1u;
    }
    const GpuInstance* lamps = s.instSrc != nullptr ? s.instSrc : s.cpuInstances;
    const uint32_t authored = count;
    for (uint32_t i = 0; i < authored; ++i) {
        LightGpuData lamp{};
        std::memcpy(&lamp, dst + sizeof(LightGpuData) * i, sizeof(lamp));
        lightPromoteCandela(lamp);
        std::memcpy(dst + sizeof(LightGpuData) * i, &lamp, sizeof(lamp));
    }
    for (uint32_t f = 0; f < s.particles.flashCount && count < kLightCap; ++f) {
        const ParticleFlash& flash = s.particles.flash[f];
        if (flash.life <= 0.0f || flash.maxLife <= 1.0e-4f) {
            continue;
        }
        LightGpuData lamp{};
        lamp.pos[0] = flash.pos[0];
        lamp.pos[1] = flash.pos[1];
        lamp.pos[2] = flash.pos[2];
        lamp.radius = flash.radius;
        lamp.color[0] = 1.0f;
        lamp.color[1] = 0.62f;
        lamp.color[2] = 0.28f;
        lamp.intensity = flash.candela * (flash.life / flash.maxLife);
        lamp.type = kLightPoint;
        lamp.flags = 4u;
        lamp.shadowMapIndex = -1;
        lamp.sourceRadius = 0.45f;
        std::memcpy(dst + sizeof(LightGpuData) * count, &lamp, sizeof(lamp));
        count += 1u;
    }
    if (((s.passMask & kPassRestir) != 0u || (s.passMask & kPassSharc) != 0u)
        && s.materialSrc != nullptr && s.materialCount > 0u && lamps != nullptr) {
        for (uint32_t i = 0; i < s.instanceCount && count < kLightCap; ++i) {
            const GpuInstance& inst = lamps[i];
            if (inst.materialId >= s.materialCount || inst.sphere[3] < 0.02f) {
                continue;
            }
            const MaterialGpuData& mat = s.materialSrc[inst.materialId];
            if (mat.emissiveIntensity <= 0.001f) {
                continue;
            }
            bool near = false;
            for (uint32_t l = 0; l < authored; ++l) {
                LightGpuData prev{};
                std::memcpy(&prev, dst + sizeof(LightGpuData) * l, sizeof(prev));
                const float dx = prev.pos[0] - inst.sphere[0];
                const float dy = prev.pos[1] - inst.sphere[1];
                const float dz = prev.pos[2] - inst.sphere[2];
                if (dx * dx + dy * dy + dz * dz < 0.25f) {
                    near = true;
                    break;
                }
            }
            if (near) {
                continue;
            }
            const float luma = 0.2126f * mat.baseColor[0] + 0.7152f * mat.baseColor[1] + 0.0722f * mat.baseColor[2];
            LightGpuData lamp{};
            lamp.pos[0] = inst.sphere[0];
            lamp.pos[1] = inst.sphere[1];
            lamp.pos[2] = inst.sphere[2];
            lamp.radius = std::max(inst.sphere[3] * 8.0f, 4.0f);
            lamp.color[0] = luma < 0.08f ? 1.0f : mat.baseColor[0];
            lamp.color[1] = luma < 0.08f ? 1.0f : mat.baseColor[1];
            lamp.color[2] = luma < 0.08f ? 1.0f : mat.baseColor[2];
            lamp.intensity = mat.emissiveIntensity * 8.0f * kPhotometric;
            lamp.type = kLightPoint;
            lamp.flags = 2u;
            lamp.sourceRadius = std::max(inst.sphere[3] * 0.35f, 0.15f);
            lamp.shadowMapIndex = -1;
            std::memcpy(dst + sizeof(LightGpuData) * count, &lamp, sizeof(lamp));
            count += 1u;
        }
    }
    if (count < kLightCap) {
        std::memset(dst + sizeof(LightGpuData) * count, 0, sizeof(LightGpuData) * (kLightCap - count));
    }
    return count;
}

void writeGroupSpheres(const GpuInstance* instances, GpuMeshlet* meshlets, uint32_t begin, uint32_t count, uint32_t meshletCount) {
    struct Box {
        uint32_t group = 0;
        float minX = 1.0e30f;
        float minY = 1.0e30f;
        float minZ = 1.0e30f;
        float maxX = -1.0e30f;
        float maxY = -1.0e30f;
        float maxZ = -1.0e30f;
    };
    std::vector<Box> boxes;
    boxes.reserve(64);
    for (uint32_t n = 0; n < count; ++n) {
        const GpuInstance& inst = instances[begin + n];
        if (inst.meshId >= meshletCount) {
            continue;
        }
        const uint32_t group = inst.pad[1] & 0x0fffffffu;
        Box* box = nullptr;
        for (Box& item : boxes) {
            if (item.group == group) {
                box = &item;
                break;
            }
        }
        if (box == nullptr) {
            boxes.push_back({});
            box = &boxes.back();
            box->group = group;
            box->minX = box->minY = box->minZ = 1.0e30f;
            box->maxX = box->maxY = box->maxZ = -1.0e30f;
        }
        const float x = inst.sphere[0];
        const float y = inst.sphere[1];
        const float z = inst.sphere[2];
        const float r = inst.sphere[3];
        box->minX = std::min(box->minX, x - r);
        box->minY = std::min(box->minY, y - r);
        box->minZ = std::min(box->minZ, z - r);
        box->maxX = std::max(box->maxX, x + r);
        box->maxY = std::max(box->maxY, y + r);
        box->maxZ = std::max(box->maxZ, z + r);
    }
    for (uint32_t n = 0; n < count; ++n) {
        const GpuInstance& inst = instances[begin + n];
        if (inst.meshId >= meshletCount) {
            continue;
        }
        const uint32_t group = inst.pad[1] & 0x0fffffffu;
        const Box* box = nullptr;
        for (const Box& item : boxes) {
            if (item.group == group) {
                box = &item;
                break;
            }
        }
        if (box == nullptr) {
            continue;
        }
        GpuMeshlet& mesh = meshlets[inst.meshId];
        mesh.groupCenter[0] = (box->minX + box->maxX) * 0.5f;
        mesh.groupCenter[1] = (box->minY + box->maxY) * 0.5f;
        mesh.groupCenter[2] = (box->minZ + box->maxZ) * 0.5f;
        const float dx = box->maxX - mesh.groupCenter[0];
        const float dy = box->maxY - mesh.groupCenter[1];
        const float dz = box->maxZ - mesh.groupCenter[2];
        mesh.groupRadius = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
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

} // namespace

struct SceneFrame : SceneFrameImpl {};

SceneFrame* sceneFrameCreate() {
    SceneFrame* scene = new SceneFrame{};
    scene->block.scalars[kScGtaoRadius] = 0.5f;
    scene->block.scalars[kScGtaoSteps] = 3.0f;
    scene->block.scalars[kScGtaoSlices] = 3.0f;
    scene->block.scalars[kScGtaoPower] = 2.2f;
    scene->block.scalars[kScGtaoFalloff] = 0.615f;
    scene->block.scalars[kScGtaoDenoise] = 1.0f;
    scene->block.scalars[kScSsrCutoff] = 0.82f;
    scene->block.scalars[kScSsrSteps] = 64.0f;
    scene->block.scalars[kScSsrStride] = 1.0f;
    scene->block.scalars[kScSsrThick] = 0.02f;
    scene->block.scalars[kScSsrMetal] = 0.35f;
    scene->block.scalars[kScSsrRt] = 0.45f;
    scene->block.scalars[kScSsrEdge] = 24.0f;
    scene->block.scalars[kScSsrMax] = 80.0f;
    scene->block.scalars[kScSsrBias] = 0.15f;
    scene->block.scalars[kScFogDensity] = 0.06f;
    scene->block.scalars[kScFogHeight] = 0.0f;
    scene->block.scalars[kScFogScatter] = 40.0f;
    scene->block.scalars[kScFogFalloff] = 0.08f;
    scene->block.scalars[kScFogR] = 0.62f;
    scene->block.scalars[kScFogG] = 0.72f;
    scene->block.scalars[kScFogB] = 0.86f;
    scene->block.scalars[kScFogLum] = 1.4f;
    scene->block.scalars[kScFogStart] = 0.0f;
    scene->block.scalars[kScFogOpacity] = 0.92f;
    scene->block.scalars[kScFogInStart] = 0.0f;
    scene->audio = audioCreate();
    testSceneBuild(scene->world);
    pointShadowAssign(scene->world.lights, scene->world.lightCount);
    scene->fly.eye[0] = scene->world.eye[0];
    scene->fly.eye[1] = scene->world.eye[1];
    scene->fly.eye[2] = scene->world.eye[2];
    placeActors(*scene);
    pushBlob(*scene, scene->world.floor, scene->world.objects[0].x, scene->world.objects[0].y, scene->world.objects[0].z);
    pushBlob(*scene, scene->world.cubes[0], scene->world.objects[1].x, scene->world.objects[1].y, scene->world.objects[1].z);
    pushBlob(*scene, scene->world.cubes[1], scene->world.objects[2].x, scene->world.objects[2].y, scene->world.objects[2].z);
    pushBlob(*scene, scene->world.cubes[2], scene->world.objects[3].x, scene->world.objects[3].y, scene->world.objects[3].z);
    for (uint32_t i = 0; i < scene->world.lightCount; ++i) {
        scene->world.lights[i].radius = 8.0f;
    }
    scene->vertSrc = scene->cpuVerts;
    scene->indexSrc = scene->cpuIndex;
    scene->instSrc = scene->cpuInstances;
    scene->meshletSrc = scene->cpuMeshlets;
    scene->visibleCap = kSceneInstances;
    scene->visibleCpu.assign(scene->visibleCap, 0u);
    const float palette[4][3] = {{0.35f, 0.34f, 0.32f}, {0.80f, 0.25f, 0.20f}, {0.20f, 0.45f, 0.85f}, {0.85f, 0.75f, 0.30f}};
    for (uint32_t i = 0; i < 4; ++i) {
        scene->fallbackMat[i] = {};
        scene->fallbackMat[i].baseColor[0] = palette[i][0];
        scene->fallbackMat[i].baseColor[1] = palette[i][1];
        scene->fallbackMat[i].baseColor[2] = palette[i][2];
        scene->fallbackMat[i].baseColor[3] = 1.0f;
        scene->fallbackMat[i].roughness = 0.42f;
    }
    scene->materialSrc = scene->fallbackMat;
    scene->materialCount = 4;
    if (scene->instanceCount > 1) {
        linkProp(*scene, 1, "Cube");
        scene->actors.selected = 0;
    }
    armEditorRoom(*scene);
    return scene;
}

void modelLabel(const SceneFrameImpl& scene, uint32_t mat, char* dst, uint32_t cap) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (scene.materialSrc != nullptr && mat < scene.materialCount && scene.texNames != nullptr) {
        const uint32_t tex = scene.materialSrc[mat].albedoTexIndex;
        if (tex > 0u && tex < scene.texNameCount) {
            const char* file = scene.texNames[tex].file;
            const char* leaf = file;
            for (const char* p = file; *p != '\0'; ++p) {
                if (*p == '/' || *p == '\\') {
                    leaf = p + 1;
                }
            }
            uint32_t n = 0;
            while (leaf[n] != '\0' && leaf[n] != '.' && n + 1u < cap) {
                dst[n] = leaf[n];
                n += 1u;
            }
            dst[n] = '\0';
        }
    }
    if (dst[0] == '\0') {
        std::snprintf(dst, cap, "Mesh %u", mat);
    }
}

void adoptLoadedModel(SceneFrame& scene, const char* path, float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    if (scene.instSrc == nullptr || scene.instanceCount == 0) {
        return;
    }
    const char* leaf = path != nullptr ? path : "Model";
    for (const char* p = leaf; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            leaf = p + 1;
        }
    }
    char name[24]{};
    uint32_t n = 0;
    for (const char* p = leaf; *p != '\0' && *p != '.' && n + 1u < sizeof(name); ++p) {
        name[n++] = *p;
    }
    if (n == 0) {
        name[0] = 'M';
        name[1] = '\0';
    }
    const float pos[3] = {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
    const uint8_t actor = sceneActorAdd(scene.actors, SceneActorKind::Prop, pos, name);
    if (actor == kSceneActorNone || actor >= scene.actors.count) {
        spdlog::error("actor for {} was not created", name);
        return;
    }
    flecs::entity ent = scene.actors.ent[actor];
    ActorBounds box{};
    box.half[0] = std::max((maxX - minX) * 0.5f, 0.01f);
    box.half[1] = std::max((maxY - minY) * 0.5f, 0.01f);
    box.half[2] = std::max((maxZ - minZ) * 0.5f, 0.01f);
    ent.set<ActorBounds>(box);
    const GpuInstance& inst = scene.instSrc[0];
    MeshBind bind{};
    bind.material = inst.materialId;
    bind.instance = 0;
    bind.anchor[0] = pos[0] - inst.world[3];
    bind.anchor[1] = pos[1] - inst.world[7];
    bind.anchor[2] = pos[2] - inst.world[11];
    ent.set<MeshBind>(bind);
    MeshSpan span{};
    span.begin = 0;
    span.count = scene.instanceCount;
    ent.set<MeshSpan>(span);
    if (scene.materialSrc != nullptr && inst.materialId < scene.materialCount) {
        const MaterialGpuData& src = scene.materialSrc[inst.materialId];
        Material pbr{};
        pbr.base[0] = src.baseColor[0];
        pbr.base[1] = src.baseColor[1];
        pbr.base[2] = src.baseColor[2];
        pbr.base[3] = src.baseColor[3];
        pbr.roughness = src.roughness;
        pbr.metallic = src.metallic;
        pbr.emissive = src.emissiveIntensity;
        ent.set<Material>(pbr);
    }
    scene.materialCap = scene.materialCount + kEditorSpare;
    spdlog::info("actor {} index {} inst {} bounds {:.3f} {:.3f} {:.3f}", name, actor, scene.instanceCount, box.half[0], box.half[1], box.half[2]);
}

bool sceneFrameLoadBhop(SceneFrame* scene, const char* path, bool plant) {
    if (scene == nullptr || path == nullptr) {
        return false;
    }
    static_assert(sizeof(SceneVertex) == sizeof(GpuVertex));
    static_assert(sizeof(SceneInstance) == sizeof(GpuInstance));
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        spdlog::error("load map failed {}", path);
        return false;
    }
    SceneBlobView view{};
    if (!sceneBlobView(mapped.data, mapped.size, &view) || view.info->instanceCount == 0) {
        spdlog::error("load blob rejected {} bytes {}", path, mapped.size);
        fileUnmap(&mapped);
        return false;
    }
    fileUnmap(&scene->bhop);
    scene->bhop = mapped;
    strCopyCap(scene->bhopPath, sizeof(scene->bhopPath), path);
    scene->meshletGpu.assign(view.info->meshletCount, GpuMeshlet{});
    float minX = view.instances[0].sphere[0];
    float minY = view.instances[0].sphere[1];
    float minZ = view.instances[0].sphere[2];
    float maxX = minX;
    float maxY = minY;
    float maxZ = minZ;
    for (uint32_t i = 0; i < view.info->meshletCount; ++i) {
        const Meshlet& src = view.meshlets[i];
        GpuMeshlet& dst = scene->meshletGpu[i];
        dst = {};
        dst.vertexOffset = src.vertexOffset;
        dst.indexOffset = src.triangleOffset * 3u;
        dst.vertexCount = src.vertexCount;
        dst.triangleCount = src.triangleCount;
        dst.coneAxis[0] = src.coneAxis[0];
        dst.coneAxis[1] = src.coneAxis[1];
        dst.coneAxis[2] = src.coneAxis[2];
        dst.coneCutoff = src.coneCutoff;
    }
    writeGroupSpheres(reinterpret_cast<const GpuInstance*>(view.instances), scene->meshletGpu.data(), 0, view.info->instanceCount, view.info->meshletCount);
    for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
        const float x = view.instances[i].sphere[0];
        const float y = view.instances[i].sphere[1];
        const float z = view.instances[i].sphere[2];
        const float r = view.instances[i].sphere[3];
        if (x - r < minX) minX = x - r;
        if (y - r < minY) minY = y - r;
        if (z - r < minZ) minZ = z - r;
        if (x + r > maxX) maxX = x + r;
        if (y + r > maxY) maxY = y + r;
        if (z + r > maxZ) maxZ = z + r;
    }
    scene->visibleCpu.assign(view.info->instanceCount, 0u);
    scene->visibleCap = static_cast<uint32_t>(scene->visibleCpu.size());
    scene->vertCount = view.info->vertexCount;
    scene->indexCount = view.info->indexCount;
    scene->instanceCount = view.info->instanceCount;
    scene->vertSrc = reinterpret_cast<const GpuVertex*>(view.vertices);
    scene->indexSrc = view.indices;
    scene->instSrc = reinterpret_cast<const GpuInstance*>(view.instances);
    scene->meshletSrc = scene->meshletGpu.data();
    materialOwnFromView(*scene, view);
    scene->texNames = view.textures;
    scene->texNameCount = view.info->textureCount;
    scene->cones = view.meshlets;
    scene->coneCount = view.info->meshletCount;
    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    const float cz = (minZ + maxZ) * 0.5f;
    const float extent = std::max(maxX - minX, std::max(maxY - minY, maxZ - minZ));
    scene->boxMin[0] = minX;
    scene->boxMin[1] = minY;
    scene->boxMin[2] = minZ;
    scene->boxMax[0] = maxX;
    scene->boxMax[1] = maxY;
    scene->boxMax[2] = maxZ;
    scene->shadowExtent = std::max(20.0f, extent);
    scene->importedCount = std::min(view.info->lightCount, kLightCap);
    if (view.lights != nullptr && scene->importedCount > 0) {
        std::memcpy(scene->imported, view.lights, sizeof(LightGpuData) * scene->importedCount);
        int32_t slot = 0;
        for (uint32_t i = 0; i < scene->importedCount; ++i) {
            scene->imported[i].shadowMapIndex = -1;
            if (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot) {
                if (scene->imported[i].radius < 8.0f) {
                    scene->imported[i].radius = 22.0f;
                }
                if (scene->imported[i].intensity < 4.0f) {
                    scene->imported[i].intensity = 8.0f;
                }
            }
            if (scene->imported[i].type == kLightDirectional) {
                scene->world.sunDir[0] = scene->imported[i].direction[0];
                scene->world.sunDir[1] = scene->imported[i].direction[1];
                scene->world.sunDir[2] = scene->imported[i].direction[2];
                continue;
            }
            if (slot < 3 && (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot)) {
                scene->imported[i].shadowMapIndex = slot;
                scene->world.lights[slot] = scene->imported[i];
                slot += 1;
            }
        }
        if (slot > 0) {
            scene->world.lightCount = static_cast<uint32_t>(slot);
        }
    }
    const float ground = minY + 2.0f;
    if (scene->importedCount == 0 && scene->world.lightCount >= 3) {
        scene->world.lights[0].pos[0] = cx;
        scene->world.lights[0].pos[1] = ground + 3.0f;
        scene->world.lights[0].pos[2] = cz;
        scene->world.lights[1].pos[0] = cx + 12.0f;
        scene->world.lights[1].pos[1] = ground + 4.0f;
        scene->world.lights[1].pos[2] = cz + 6.0f;
        scene->world.lights[2].pos[0] = cx - 10.0f;
        scene->world.lights[2].pos[1] = ground + 5.0f;
        scene->world.lights[2].pos[2] = cz - 8.0f;
        for (uint32_t i = 0; i < 3; ++i) {
            scene->world.lights[i].radius = 24.0f;
        }
    }
    scene->farPlane = std::max(200.0f, extent * 4.0f);
    const float span = std::max(extent, 1.0e-3f);
    if (span < 4.0f) {
        scene->nearPlane = std::max(0.001f, std::min(0.05f, span * 0.02f));
        scene->fly.speed = std::max(0.15f, span * 0.8f);
    } else {
        scene->fly.speed = std::max(4.0f, span * 0.02f);
    }
    const float back = span < 4.0f ? std::max(span * 2.2f, 0.35f) : span * 0.05f;
    const float lift = span < 4.0f ? span * 0.35f : span * 0.15f;
    scene->fly.eye[0] = cx;
    scene->fly.eye[1] = cy + lift;
    scene->fly.eye[2] = maxZ + back;
    scene->fly.yaw = 0.0f;
    scene->fly.pitch = -0.15f;
    placeActors(*scene);
    scene->propCount = 0;
    scene->propPhase = 0;
    scene->propDirty = 0;
    scene->ready = false;
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    if (plant) {
        sceneFramePlantCubes(scene);
    } else {
        adoptLoadedModel(*scene, path, minX, minY, minZ, maxX, maxY, maxZ);
    }
    uint32_t hand = 0;
    for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
        if ((view.instances[i].pad[1] & 0x0fffffffu) >= kSceneLighterGroup) {
            hand += 1u;
        }
    }
    spdlog::info(
        "load {} verts {} idx {} inst {} mesh {} mat {} tex {} box {:.3f} {:.3f} {:.3f} .. {:.3f} {:.3f} {:.3f} eye {:.3f} {:.3f} {:.3f} yaw {:.2f} pitch {:.2f} near {:.4f} far {:.1f} speed {:.2f} hand {}",
        path, scene->vertCount, scene->indexCount, scene->instanceCount, view.info->meshletCount,
        scene->materialCount, scene->texNameCount,
        minX, minY, minZ, maxX, maxY, maxZ,
        scene->fly.eye[0], scene->fly.eye[1], scene->fly.eye[2],
        scene->fly.yaw, scene->fly.pitch, scene->nearPlane, scene->farPlane, scene->fly.speed, hand);
    return true;
}

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

void freeOwned(SceneFrame& s) {
    if (s.vertSrc == s.ownedVert.data()) {
        s.vertSrc = nullptr;
    }
    if (s.indexSrc == s.ownedIndex.data()) {
        s.indexSrc = nullptr;
    }
    if (s.instSrc == s.ownedInst.data()) {
        s.instSrc = nullptr;
    }
    if (s.meshletSrc == s.ownedMesh.data()) {
        s.meshletSrc = nullptr;
    }
    if (s.materialSrc == s.ownedMat.data()) {
        s.materialSrc = nullptr;
    }
    if (s.texNames == s.ownedTex.data()) {
        s.texNames = nullptr;
    }
    s.ownedVert.clear();
    s.ownedIndex.clear();
    s.ownedInst.clear();
    s.ownedMesh.clear();
    s.ownedMat.clear();
    s.ownedTex.clear();
}

uint32_t remapTex(uint32_t index, uint32_t texBase) {
    return index == 0u ? 0u : texBase + index - 1u;
}

void sceneFrameShift(SceneFrame* scene, float dx, float dy, float dz) {
    if (scene == nullptr || scene->instanceCount == 0 || scene->instSrc == nullptr) {
        return;
    }
    if (scene->instSrc != scene->ownedInst.data()) {
        scene->ownedInst.assign(scene->instSrc, scene->instSrc + scene->instanceCount);
        scene->instSrc = scene->ownedInst.data();
    }
    for (uint32_t i = 0; i < scene->instanceCount; ++i) {
        GpuInstance& inst = scene->ownedInst[i];
        inst.world[3] += dx;
        inst.world[7] += dy;
        inst.world[11] += dz;
        inst.sphere[0] += dx;
        inst.sphere[1] += dy;
        inst.sphere[2] += dz;
    }
    scene->boxMin[0] += dx;
    scene->boxMin[1] += dy;
    scene->boxMin[2] += dz;
    scene->boxMax[0] += dx;
    scene->boxMax[1] += dy;
    scene->boxMax[2] += dz;
}

void sceneFrameBlank(SceneFrame* scene) {
    if (scene == nullptr) {
        return;
    }
    fileUnmap(&scene->bhop);
    scene->bhopPath[0] = '\0';
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    freeOwned(*scene);
    scene->meshletGpu.clear();
    scene->vertCount = 0;
    scene->indexCount = 0;
    scene->instanceCount = 0;
    scene->materialCount = 0;
    scene->texNameCount = 0;
    scene->propCount = 0;
    scene->vertSrc = scene->cpuVerts;
    scene->indexSrc = scene->cpuIndex;
    scene->instSrc = scene->cpuInstances;
    scene->meshletSrc = scene->cpuMeshlets;
    scene->materialSrc = nullptr;
    scene->texNames = nullptr;
    scene->cones = nullptr;
    scene->coneCount = 0;
    placeActors(*scene);
    armEditorRoom(*scene);
    scene->ready = false;
}

bool sceneFrameAppendBhop(SceneFrame* scene, const char* path) {
    if (scene == nullptr || path == nullptr) {
        return false;
    }
    spdlog::info("append {} base verts {} inst {}", path, scene->vertCount, scene->instanceCount);
    if (scene->vertSrc == nullptr || scene->vertCount == 0 || scene->instanceCount == 0) {
        spdlog::info("append empty scene, load {}", path);
        return sceneFrameLoadBhop(scene, path, false);
    }
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        spdlog::error("append map failed {}", path);
        return false;
    }
    SceneBlobView view{};
    if (!sceneBlobView(mapped.data, mapped.size, &view) || view.info->instanceCount == 0) {
        spdlog::error("append blob rejected {} bytes {}", path, mapped.size);
        fileUnmap(&mapped);
        return false;
    }
    const uint32_t vertBase = scene->vertCount;
    const uint32_t indexBase = scene->indexCount;
    const uint32_t instBase = scene->instanceCount;
    const uint32_t matBase = scene->materialCount;
    const uint32_t texBase = scene->texNameCount == 0 ? 1u : scene->texNameCount;
    const uint32_t addTex = view.info->textureCount > 0 ? view.info->textureCount - 1u : 0u;
    const uint32_t vertCount = vertBase + view.info->vertexCount + 4u;
    const uint32_t indexCount = indexBase + view.info->indexCount + 6u;
    const uint32_t instCount = instBase + view.info->instanceCount + 1u;
    const uint32_t matCount = matBase + view.info->materialCount + 1u;
    const uint32_t texCount = texBase + addTex;
    std::vector<GpuVertex> verts(vertCount);
    std::vector<uint32_t> indices(indexCount);
    std::vector<GpuInstance> instances(instCount);
    std::vector<GpuMeshlet> meshlets(instCount);
    std::vector<MaterialGpuData> materials(matCount == 0 ? 1u : matCount);
    std::vector<SceneTexName> names(texCount == 0 ? 1u : texCount);
    if (vertBase > 0) {
        std::memcpy(verts.data(), scene->vertSrc, sizeof(GpuVertex) * vertBase);
    }
    if (indexBase > 0) {
        std::memcpy(indices.data(), scene->indexSrc, sizeof(uint32_t) * indexBase);
    }
    if (instBase > 0) {
        std::memcpy(instances.data(), scene->instSrc, sizeof(GpuInstance) * instBase);
        std::memcpy(meshlets.data(), scene->meshletSrc, sizeof(GpuMeshlet) * instBase);
    }
    if (scene->materialSrc != nullptr && matBase > 0) {
        std::memcpy(materials.data(), scene->materialSrc, sizeof(MaterialGpuData) * matBase);
    }
    if (scene->texNames != nullptr && scene->texNameCount > 0) {
        std::memcpy(names.data(), scene->texNames, sizeof(SceneTexName) * scene->texNameCount);
    }
    std::memcpy(verts.data() + vertBase, view.vertices, sizeof(GpuVertex) * view.info->vertexCount);
    std::memcpy(indices.data() + indexBase, view.indices, sizeof(uint32_t) * view.info->indexCount);
    float minX = 1.0e9f;
    float minY = 1.0e9f;
    float minZ = 1.0e9f;
    float maxX = -1.0e9f;
    float maxY = -1.0e9f;
    float maxZ = -1.0e9f;
    for (uint32_t i = 0; i < view.info->instanceCount; ++i) {
        GpuInstance& dst = instances[instBase + i];
        const SceneInstance& src = view.instances[i];
        std::copy_n(src.world, std::size(src.world), dst.world);
        std::copy_n(src.sphere, std::size(src.sphere), dst.sphere);
        dst.materialId = src.materialId;
        dst.meshId = src.meshId;
        std::copy_n(src.pad, std::size(src.pad), dst.pad);
        dst.materialId += matBase;
        dst.meshId += instBase;
        const float x = dst.sphere[0];
        const float y = dst.sphere[1];
        const float z = dst.sphere[2];
        const float r = dst.sphere[3];
        minX = std::min(minX, x - r);
        minY = std::min(minY, y - r);
        minZ = std::min(minZ, z - r);
        maxX = std::max(maxX, x + r);
        maxY = std::max(maxY, y + r);
        maxZ = std::max(maxZ, z + r);
    }
    for (uint32_t i = 0; i < view.info->meshletCount && instBase + i < instCount; ++i) {
        const Meshlet& src = view.meshlets[i];
        GpuMeshlet& dst = meshlets[instBase + i];
        dst = {};
        dst.vertexOffset = src.vertexOffset + vertBase;
        dst.indexOffset = src.triangleOffset * 3u + indexBase;
        dst.vertexCount = src.vertexCount;
        dst.triangleCount = src.triangleCount;
        dst.coneAxis[0] = src.coneAxis[0];
        dst.coneAxis[1] = src.coneAxis[1];
        dst.coneAxis[2] = src.coneAxis[2];
        dst.coneCutoff = src.coneCutoff;
    }
    writeGroupSpheres(instances.data(), meshlets.data(), instBase, view.info->instanceCount, static_cast<uint32_t>(meshlets.size()));
    for (uint32_t i = 0; i < addTex; ++i) {
        names[texBase + i] = view.textures[i + 1u];
    }
    for (uint32_t i = 0; i < view.info->materialCount; ++i) {
        MaterialGpuData& dst = materials[matBase + i];
        const uint8_t* raw = view.materialRaw != nullptr ? view.materialRaw + static_cast<size_t>(i) * view.materialStride : nullptr;
        materialAdoptRecord(dst, raw, view.materialStride);
        dst.albedoTexIndex = remapTex(dst.albedoTexIndex, texBase);
        dst.normalTexIndex = remapTex(dst.normalTexIndex, texBase);
        dst.ormTexIndex = remapTex(dst.ormTexIndex, texBase);
        dst.emissiveTexIndex = remapTex(dst.emissiveTexIndex, texBase);
        dst.pad[0] = remapTex(dst.pad[0], texBase);
        dst.pad[2] = remapTex(dst.pad[2], texBase);
        dst.pad[3] = remapTex(dst.pad[3], texBase);
        dst.aoTex = remapTex(dst.aoTex, texBase);
    }
    const uint32_t mirrorVert = vertBase + view.info->vertexCount;
    const uint32_t mirrorIndex = indexBase + view.info->indexCount;
    const uint32_t mirrorInst = instBase + view.info->instanceCount;
    const uint32_t mirrorMat = matBase + view.info->materialCount;
    const float mirrorX = (minX + maxX) * 0.5f;
    const float mirrorY = minY + 1.6f;
    const float mirrorZ = maxZ - 4.0f;
    const float mirrorW = 1.2f;
    const float mirrorH = 1.8f;
    const float mirrorPos[4][4] = {
        {mirrorX - mirrorW, mirrorY - mirrorH, 0.0f, 1.0f},
        {mirrorX + mirrorW, mirrorY - mirrorH, 1.0f, 1.0f},
        {mirrorX + mirrorW, mirrorY + mirrorH, 1.0f, 0.0f},
        {mirrorX - mirrorW, mirrorY + mirrorH, 0.0f, 0.0f},
    };
    for (uint32_t i = 0; i < 4u; ++i) {
        GpuVertex& vert = verts[mirrorVert + i];
        vert = {};
        vert.px = mirrorPos[i][0];
        vert.py = mirrorPos[i][1];
        vert.pz = mirrorZ;
        vert.pad0 = mirrorPos[i][2];
        vert.nz = 1.0f;
        vert.pad1 = mirrorPos[i][3];
    }
    const uint32_t mirrorTri[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    for (uint32_t i = 0; i < 6u; ++i) {
        indices[mirrorIndex + i] = mirrorTri[i];
    }
    GpuMeshlet& mirrorMesh = meshlets[mirrorInst];
    mirrorMesh = {};
    mirrorMesh.vertexOffset = mirrorVert;
    mirrorMesh.indexOffset = mirrorIndex;
    mirrorMesh.vertexCount = 4;
    mirrorMesh.triangleCount = 2;
    mirrorMesh.coneAxis[2] = 1.0f;
    mirrorMesh.coneCutoff = -1.0f;
    instanceSet(instances[mirrorInst], 0.0f, 0.0f, 0.0f, 3.0f, mirrorMat, mirrorInst);
    instances[mirrorInst].sphere[0] = mirrorX;
    instances[mirrorInst].sphere[1] = mirrorY;
    instances[mirrorInst].sphere[2] = mirrorZ;
    instances[mirrorInst].sphere[3] = 3.0f;
    MaterialGpuData& mirror = materials[mirrorMat];
    mirror = {};
    mirror.baseColor[0] = mirror.baseColor[1] = mirror.baseColor[2] = mirror.baseColor[3] = 1.0f;
    mirror.roughness = 0.02f;
    mirror.metallic = 1.0f;
    freeOwned(*scene);
    scene->ownedVert = std::move(verts);
    scene->ownedIndex = std::move(indices);
    scene->ownedInst = std::move(instances);
    scene->ownedMesh = std::move(meshlets);
    scene->ownedMat = std::move(materials);
    scene->ownedTex = std::move(names);
    scene->vertSrc = scene->ownedVert.data();
    scene->indexSrc = scene->ownedIndex.data();
    scene->instSrc = scene->ownedInst.data();
    scene->propCount = 0;
    scene->propPhase = 0;
    scene->propDirty = 0;
    scene->meshletSrc = scene->ownedMesh.data();
    scene->materialSrc = scene->ownedMat.data();
    scene->texNames = scene->ownedTex.data();
    scene->vertCount = vertCount;
    scene->indexCount = indexCount;
    scene->instanceCount = instCount;
    scene->materialCount = matCount;
    scene->texNameCount = texCount;
    scene->visibleCpu.assign(instCount, 0u);
    scene->visibleCap = static_cast<uint32_t>(scene->visibleCpu.size());
    scene->meshletGpu.clear();
    scene->cones = nullptr;
    scene->coneCount = 0;
    if (view.lights != nullptr && view.info->lightCount > 0) {
        LightGpuData merged[kLightCap]{};
        uint32_t n = 0;
        const uint32_t add = std::min(view.info->lightCount, kLightCap);
        std::memcpy(merged, view.lights, sizeof(LightGpuData) * add);
        n = add;
        for (uint32_t i = 0; i < scene->importedCount && n < kLightCap; ++i) {
            merged[n++] = scene->imported[i];
        }
        std::memcpy(scene->imported, merged, sizeof(LightGpuData) * n);
        scene->importedCount = n;
        int32_t slot = 0;
        scene->world.lightCount = 0;
        for (uint32_t i = 0; i < n; ++i) {
            scene->imported[i].shadowMapIndex = -1;
            if (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot) {
                if (scene->imported[i].radius < 8.0f) {
                    scene->imported[i].radius = 22.0f;
                }
                if (scene->imported[i].intensity < 4.0f) {
                    scene->imported[i].intensity = 8.0f;
                }
            }
            if (scene->imported[i].type == kLightDirectional) {
                scene->world.sunDir[0] = scene->imported[i].direction[0];
                scene->world.sunDir[1] = scene->imported[i].direction[1];
                scene->world.sunDir[2] = scene->imported[i].direction[2];
                continue;
            }
            if (slot < 3 && (scene->imported[i].type == kLightPoint || scene->imported[i].type == kLightSpot)) {
                scene->imported[i].shadowMapIndex = slot;
                scene->world.lights[slot] = scene->imported[i];
                slot += 1;
            }
        }
        scene->world.lightCount = static_cast<uint32_t>(slot);
    }
    const float extent = std::max(maxX - minX, std::max(maxY - minY, maxZ - minZ));
    scene->shadowExtent = std::max(20.0f, extent);
    scene->farPlane = std::max(scene->farPlane, std::max(400.0f, extent * 4.0f));
    scene->farPlane = std::max(scene->farPlane, 8000.0f);
    scene->fly.eye[0] = (minX + maxX) * 0.5f;
    scene->fly.eye[1] = std::max(2.0f, minY + std::max(2.0f, extent * 0.15f));
    scene->fly.eye[2] = maxZ + std::max(4.0f, extent * 0.05f);
    scene->fly.yaw = 0.0f;
    scene->fly.pitch = -0.1f;
    scene->fly.speed = 40.0f;
    placeActors(*scene);
    scene->boxMin[0] = std::min(scene->boxMin[0], minX);
    scene->boxMin[1] = std::min(scene->boxMin[1], minY);
    scene->boxMin[2] = std::min(scene->boxMin[2], minZ);
    scene->boxMax[0] = std::max(scene->boxMax[0], maxX);
    scene->boxMax[1] = std::max(scene->boxMax[1], maxY);
    scene->boxMax[2] = std::max(scene->boxMax[2], maxZ);
    spdlog::info(
        "append merged {} added inst {} verts {} now inst {} verts {} eye {:.3f} {:.3f} {:.3f}",
        path, view.info->instanceCount, view.info->vertexCount, scene->instanceCount, scene->vertCount,
        scene->fly.eye[0], scene->fly.eye[1], scene->fly.eye[2]);
    fileUnmap(&mapped);
    armEditorRoom(*scene);
    const char* leaf = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            leaf = p + 1;
        }
    }
    const uint32_t added = view.info->instanceCount < 8u ? view.info->instanceCount : 8u;
    for (uint32_t i = 0; i < added && scene->propCount < kPropCap; ++i) {
        char name[24]{};
        if (scene->instSrc != nullptr && instBase + i < scene->instanceCount) {
            modelLabel(*scene, scene->instSrc[instBase + i].materialId, name, sizeof(name));
        }
        if (name[0] == '\0') {
            std::snprintf(name, sizeof(name), "%s", leaf);
        }
        linkProp(*scene, instBase + i, name);
    }
    scene->ready = false;
    return true;
}

HudPost sceneBlockPost(const SceneBlock& block) {
    HudPost post{};
    const std::span<const float> s = sceneBlockSpan(block, kScPost, kScPostN);
    if (s.size() != kScPostN) {
        return post;
    }
    post.grain = s[0];
    post.brightness = s[1];
    post.bloom = s[2];
    post.vignette = s[3];
    post.ca = s[4];
    post.contact = s[5];
    post.specOcc = s[6];
    post.toksvig = s[7];
    post.pom = s[8];
    post.ggx = s[9];
    post.ssrc = s[10];
    post.gtao = s[11];
    post.lamp = s[12];
    post.sunGain = s[13];
    post.sunR = s[14];
    post.sunG = s[15];
    post.sunB = s[16];
    post.exposure = s[17];
    post.temperature = s[18];
    post.tint = s[19];
    post.contrast = s[20];
    post.saturation = s[21];
    post.night = s[22];
    post.shadows = s[23];
    post.midtones = s[24];
    post.highlights = s[25];
    post.slope = s[26];
    post.cdlOffset = s[27];
    post.power = s[28];
    post.dofFocus = s[29];
    post.dofAmount = s[30];
    post.flare = s[31];
    post.gradeFlags = s[32];
    post.mix = s[33];
    post.genContrast = s[34];
    post.midIn = s[35];
    post.midOut = s[36];
    post.hdrMax = s[37];
    post.vibrance = s[38];
    return post;
}

SceneTweaks sceneBlockTweaks(const SceneBlock& block) {
    SceneTweaks tweaks{};
    const std::span<const float> gtao = sceneBlockSpan(block, kScGtao, kScGtaoN);
    const std::span<const float> ssr = sceneBlockSpan(block, kScSsr, kScSsrN);
    const std::span<const float> sscs = sceneBlockSpan(block, kScSscs, kScSscsN);
    const std::span<const float> ssrc = sceneBlockSpan(block, kScSsrc, kScSsrcN);
    const std::span<const float> rc = sceneBlockSpan(block, kScRc, kScRcN);
    const std::span<const float> restir = sceneBlockSpan(block, kScRestir, kScRestirN);
    const std::span<const float> fog = sceneBlockSpan(block, kScFog, kScFogN);
    tweaks.forceLod = static_cast<int32_t>(sceneBlockAt(block, kScForceLod));
    tweaks.passMask = block.passMask;
    tweaks.gtaoRadius = gtao[0];
    tweaks.gtaoSteps = gtao[1];
    tweaks.gtaoSlices = gtao[2];
    tweaks.gtaoPower = gtao[3];
    tweaks.gtaoFalloff = gtao[4];
    tweaks.gtaoDenoise = gtao[5];
    tweaks.ssrCutoff = ssr[0];
    tweaks.ssrSteps = ssr[1];
    tweaks.ssrStride = ssr[2];
    tweaks.ssrThick = ssr[3];
    tweaks.ssrMetal = ssr[4];
    tweaks.ssrRt = ssr[5];
    tweaks.ssrEdge = ssr[6];
    tweaks.ssrMax = ssr[7];
    tweaks.ssrBias = ssr[8];
    tweaks.sscsReach = sscs[0];
    tweaks.sscsThick = sscs[1];
    tweaks.sscsSteps = sscs[2];
    tweaks.sscsPx = sscs[3];
    tweaks.ssrcBase = ssrc[0];
    tweaks.ssrcThick = ssrc[1];
    tweaks.ssrcCap = ssrc[2];
    tweaks.ssrcSteps = ssrc[3];
    tweaks.rcRays = rc[0];
    tweaks.rcSpacing = rc[1];
    tweaks.rcIntensity = rc[2];
    tweaks.rcMax = rc[3];
    tweaks.rcSun = rc[4];
    tweaks.rcBounce = rc[5];
    tweaks.giGain = rc[6];
    tweaks.sharcDenoise = sceneBlockFlag(block, kFlSharcDenoise) != 0;
    tweaks.restirLocal = restir[0];
    tweaks.restirSpatial = restir[1];
    tweaks.restirRadius = restir[2];
    tweaks.restirHistory = restir[3];
    tweaks.restirBoil = restir[4];
    tweaks.restirTemporalDepth = restir[5];
    tweaks.restirTemporalNormal = restir[6];
    tweaks.restirTemporalBias = restir[7];
    tweaks.restirSpatialDepth = restir[8];
    tweaks.restirSpatialNormal = restir[9];
    tweaks.restirSpatialBias = restir[10];
    tweaks.restirSpatialBoost = restir[11];
    tweaks.restirSunDisk = restir[12];
    tweaks.restirSunRays = restir[13];
    tweaks.restirShadowRays = restir[14];
    tweaks.restirShadowRange = restir[15];
    tweaks.restirShadowBias = restir[16];
    tweaks.restirPermTh = restir[17];
    tweaks.restirTarget = restir[18];
    tweaks.restirTemporal = sceneBlockFlag(block, kFlRestirTemporal) != 0;
    tweaks.restirSpatialOn = sceneBlockFlag(block, kFlRestirSpatial) != 0;
    tweaks.restirBoilOn = sceneBlockFlag(block, kFlRestirBoil) != 0;
    tweaks.restirInitVis = sceneBlockFlag(block, kFlRestirInitVis) != 0;
    tweaks.restirPerm = sceneBlockFlag(block, kFlRestirPerm) != 0;
    tweaks.restirShortcut = sceneBlockFlag(block, kFlRestirShortcut) != 0;
    tweaks.restirMaterial = sceneBlockFlag(block, kFlRestirMaterial) != 0;
    tweaks.restirNaive = sceneBlockFlag(block, kFlRestirNaive) != 0;
    tweaks.restirSunOn = sceneBlockFlag(block, kFlRestirSun) != 0;
    tweaks.fsrQuality = sceneBlockFlag(block, kFlFsrQ);
    tweaks.fsrSharpness = sceneBlockAt(block, kScFsrSharpness);
    tweaks.lightProfile = sceneBlockFlag(block, kFlGi);
    tweaks.bloomThreshold = sceneBlockAt(block, kScBloomThreshold);
    tweaks.fogDensity = fog[0];
    tweaks.fogHeight = fog[1];
    tweaks.fogScatter = fog[2];
    tweaks.fogFalloff = fog[3];
    tweaks.fogR = fog[4];
    tweaks.fogG = fog[5];
    tweaks.fogB = fog[6];
    tweaks.fogLum = fog[7];
    tweaks.fogStart = fog[8];
    tweaks.fogOpacity = fog[9];
    tweaks.fogInStart = fog[10];
    tweaks.rainAmount = sceneBlockAt(block, kScRainAmount);
    tweaks.rainWet = sceneBlockAt(block, kScRainWet);
    tweaks.rainRipple = sceneBlockAt(block, kScRainRipple);
    tweaks.sunZenith = sceneBlockAt(block, kScSunZenith);
    tweaks.sunAzimuth = sceneBlockAt(block, kScSunAzimuth);
    tweaks.cloudCoverage = sceneBlockAt(block, kScCloudCoverage);
    tweaks.cloudDensity = sceneBlockAt(block, kScCloudDensity);
    tweaks.windSpeed = sceneBlockAt(block, kScWindSpeed);
    tweaks.windYaw = sceneBlockAt(block, kScWindYaw);
    tweaks.temperatureC = sceneBlockAt(block, kScTemperatureC);
    tweaks.dayLength = sceneBlockAt(block, kScDayLength);
    tweaks.timeOfDay = sceneBlockAt(block, kScTimeOfDay);
    tweaks.clockSun = sceneBlockFlag(block, kFlWxClock);
    tweaks.sunKelvin = sceneBlockAt(block, kScSunKelvin);
    tweaks.sunAngle = sceneBlockAt(block, kScSunAngle);
    tweaks.sunSoft = sceneBlockAt(block, kScSunSoft);
    tweaks.sunIndirect = sceneBlockAt(block, kScSunIndirect);
    tweaks.sunEnabled = sceneBlockFlag(block, kFlSun);
    tweaks.sunCast = sceneBlockFlag(block, kFlSunCast);
    tweaks.sunCsm = sceneBlockFlag(block, kFlSunCsm);
    tweaks.tonemapper = sceneBlockFlag(block, kFlTone);
    tweaks.freeze = sceneBlockFlag(block, kFlFreeze) != 0;
    return tweaks;
}

static_assert(kPassRestir == (1u << 20));
static_assert(kPassFsr == (1u << 22));
static_assert(kPassSharc == (1u << 23));

void sceneFrameSetBlock(SceneFrame* scene, const SceneBlock& block) {
    if (scene == nullptr) {
        return;
    }
    SceneBlock stored = block;
    stored.passMask = sceneBlockPassMask(stored);
    scene->block = stored;
    const SceneTweaks tweaks = sceneBlockTweaks(stored);
    scene->hud = sceneBlockPost(block);
    scene->forceLod = tweaks.forceLod;
    scene->passMask = tweaks.passMask;
    scene->lightProfile = tweaks.lightProfile;
    scene->sscsReach = tweaks.sscsReach;
    scene->sscsThick = tweaks.sscsThick;
    scene->sscsSteps = tweaks.sscsSteps;
    scene->sscsPx = tweaks.sscsPx;
    scene->ssrcBase = tweaks.ssrcBase;
    scene->ssrcThick = tweaks.ssrcThick;
    scene->ssrcCap = tweaks.ssrcCap;
    scene->ssrcSteps = tweaks.ssrcSteps;
    scene->rcRays = tweaks.rcRays;
    scene->rcSpacing = tweaks.rcSpacing;
    scene->rcIntensity = tweaks.rcIntensity;
    scene->rcMax = tweaks.rcMax;
    scene->rcSun = tweaks.rcSun;
    scene->rcBounce = tweaks.rcBounce;
    scene->giGain = tweaks.giGain;
    scene->sharcDenoise = tweaks.sharcDenoise;
    const std::span<const float> restir = sceneBlockSpan(block, kScRestir, kScRestirN);
    scene->restir.knobLocal = static_cast<uint32_t>(std::clamp(restir[0], 0.0f, 16.0f));
    scene->restir.knobSpatial = static_cast<uint32_t>(std::clamp(restir[1], 0.0f, 32.0f));
    scene->restir.knobRadius = std::clamp(restir[2], 1.0f, 64.0f);
    scene->restir.knobHistory = static_cast<uint32_t>(std::clamp(restir[3], 1.0f, 64.0f));
    scene->restir.knobBoil = std::clamp(restir[4], 0.0f, 1.0f);
    scene->restir.knobTemporalDepth = std::clamp(restir[5], 0.001f, 1.0f);
    scene->restir.knobTemporalNormal = std::clamp(restir[6], 0.0f, 1.0f);
    scene->restir.knobTemporalBias = static_cast<uint32_t>(std::clamp(restir[7], 0.0f, 2.0f));
    scene->restir.knobSpatialDepth = std::clamp(restir[8], 0.001f, 1.0f);
    scene->restir.knobSpatialNormal = std::clamp(restir[9], 0.0f, 1.0f);
    scene->restir.knobSpatialBias = static_cast<uint32_t>(std::clamp(restir[10], 0.0f, 3.0f));
    scene->restir.knobSpatialBoost = static_cast<uint32_t>(std::clamp(restir[11], 0.0f, 32.0f));
    scene->restir.knobSunDisk = std::clamp(restir[12], 0.0f, 4.0f);
    scene->restir.knobSunRays = static_cast<uint32_t>(std::clamp(restir[13], 1.0f, 8.0f));
    scene->restir.knobShadowRays = static_cast<uint32_t>(std::clamp(restir[14], 1.0f, 8.0f));
    scene->restir.knobShadowRange = std::clamp(restir[15], 0.0f, 200.0f);
    scene->restir.knobShadowBias = std::clamp(restir[16], 0.02f, 2.0f);
    scene->restir.knobPermTh = std::clamp(restir[17], 0.0f, 1.0f);
    scene->restir.knobTarget = static_cast<uint32_t>(std::clamp(restir[18], 1.0f, 64.0f));
    scene->restir.knobTemporalOn = sceneBlockFlag(block, kFlRestirTemporal);
    scene->restir.knobSpatialOn = sceneBlockFlag(block, kFlRestirSpatial);
    scene->restir.knobBoilOn = sceneBlockFlag(block, kFlRestirBoil);
    scene->restir.knobInitVis = sceneBlockFlag(block, kFlRestirInitVis);
    scene->restir.knobPerm = sceneBlockFlag(block, kFlRestirPerm);
    scene->restir.knobShortcut = sceneBlockFlag(block, kFlRestirShortcut);
    scene->restir.knobMaterial = sceneBlockFlag(block, kFlRestirMaterial);
    scene->restir.knobNaive = sceneBlockFlag(block, kFlRestirNaive);
    scene->restir.knobSunOn = (sceneBlockFlag(block, kFlRestirSun) != 0 && tweaks.sunEnabled != 0 && tweaks.sunCsm == 0) ? 1u : 0u;
    scene->restir.knobSunDisk = std::clamp(tweaks.sunAngle + tweaks.sunSoft, 0.0f, 8.0f);
    scene->fsr.quality = tweaks.fsrQuality > 4u ? 0u : tweaks.fsrQuality;
    scene->fsr.sharpness = std::clamp(tweaks.fsrSharpness, 0.0f, 1.0f);
    scene->bloomThreshold = tweaks.bloomThreshold;
    scene->rainRipple = std::clamp(tweaks.rainRipple, 0.0f, 1.0f);
    if (Weather* climate = scene->weatherEnt.try_get_mut<Weather>()) {
        weatherGoals(*climate, tweaks.cloudCoverage, tweaks.cloudDensity, tweaks.rainAmount, tweaks.rainWet, tweaks.fogDensity, tweaks.sunZenith, tweaks.sunAzimuth, tweaks.windSpeed, tweaks.windYaw, tweaks.temperatureC, tweaks.dayLength, tweaks.timeOfDay, tweaks.clockSun);
        if (tweaks.sunEnabled != 0) {
            climate->flags |= 2u;
        } else {
            climate->flags &= ~2u;
        }
        if (tweaks.sunCast != 0) {
            climate->flags |= 4u;
        } else {
            climate->flags &= ~4u;
        }
        if (tweaks.sunCsm != 0) {
            climate->flags |= 8u;
        } else {
            climate->flags &= ~8u;
        }
        climate->kelvin = tweaks.sunKelvin;
        climate->sunAngle = tweaks.sunAngle;
        climate->softAngle = tweaks.sunSoft;
        climate->indirect = tweaks.sunIndirect;
    } else {
        scene->rainAmount = std::clamp(tweaks.rainAmount, 0.0f, 4.0f);
        scene->rainWet = std::clamp(tweaks.rainWet, 0.0f, 1.0f);
        scene->atmosphere.sunZenith = tweaks.sunZenith;
        scene->atmosphere.sunAzimuth = tweaks.sunAzimuth;
        scene->atmosphere.cloudCoverage = tweaks.cloudCoverage;
        scene->atmosphere.cloudDensity = tweaks.cloudDensity;
    }
    scene->sunEnabled = tweaks.sunEnabled != 0;
    scene->sunCast = tweaks.sunCast != 0;
    scene->sunCsm = tweaks.sunCsm != 0;
    scene->sunKelvin = tweaks.sunKelvin;
    scene->sunIndirect = tweaks.sunIndirect;
    scene->tonemapper = tweaks.tonemapper > 7u ? 0u : tweaks.tonemapper;
    scene->freeze = tweaks.freeze;
}

const char* sceneFrameGpuText(const SceneFrame* scene) {
    if (scene == nullptr) {
        return "";
    }
    return scene->gpuText;
}

void sceneFrameReleaseGpu(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    if (device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.device);
    }
    gpuProfilerDestroy(scene->gpuTime, device);
    visPassDestroy(scene->vis, device);
    shadePassDestroy(scene->shade, device);
    skyPassDestroy(scene->sky, device);
    radianceDestroy(scene->radiance, device);
    sharcDestroy(scene->sharc, device);
    restirDestroy(scene->restir, device);
    fsrDestroy(scene->fsr, device);
    gtaoDestroy(scene->gtao, device);
    voxelDestroy(scene->voxel, device);
    hizPassDestroy(scene->hiz, device);
    cubePassDestroy(scene->cube, device);
    tonemapPassDestroy(scene->tone, device);
    gizmoPassDestroy(scene->gizmo, device);
    particlePassDestroy(scene->particles, device);
    fogDestroy(scene->fog, device);
    rainDestroy(scene->rain, device);
    destroyBuffers(device, {
        &scene->visible, &scene->shadowVisible, &scene->pointVisible, &scene->cubeVisible,
        &scene->candidates, &scene->indirect, &scene->shadowIndirect,
    });
    occlPassDestroy(scene->occl, device);
    clusterPassDestroy(scene->clusters, device);
    shadowCullDestroy(scene->shadowCull, device);
    destroyBuffers(device, {
        &scene->lightBuf, &scene->cellBuf, &scene->lightIndex, &scene->decalBuf, &scene->probeBuf,
        &scene->sectorBuf, &scene->shadowPageBuf, &scene->texPageBuf, &scene->texFeedbackBuf,
        &scene->meshlets, &scene->instances, &scene->indices, &scene->verts,
        &scene->frameBuf, &scene->sunBuf, &scene->postBuf, &scene->atmBuf, &scene->materialsBuf,
    });
    destroyImages(device, {
        &scene->shadowColor[0], &scene->shadowColor[1],
    });
    destroyTracked(device, {
        &scene->cubeColor, &scene->cubeDepth,
        &scene->shadowDepth[0], &scene->shadowDepth[1],
        &scene->pointDepth,
        &scene->hizMax[0], &scene->hizMax[1],
    });
    scene->pointReady = false;
    scene->cubeReady = false;
    for (GpuImage& img : scene->textures) {
        gpuImageDestroy(img, device);
    }
    scene->textures.clear();
    scene->textureSlots = 0;
    descriptorHeapsDestroy(scene->gpuHeap, device);
    scene->ready = false;
}

void sceneFrameDestroy(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    sceneFrameReleaseGpu(scene, device);
    physicsDestroy(scene->physics);
    scene->physics = nullptr;
    audioDestroy(scene->audio);
    scene->audio = nullptr;
    fileUnmap(&scene->bhop);
    freeOwned(*scene);
    scene->meshletGpu.clear();
    scene->visibleCpu.clear();
    delete scene;
}

bool sceneFrameTick(SceneFrame* scene, const InputFrame& input, float dt) {
    if (scene == nullptr) {
        return false;
    }
    float gpu = 0.0f;
    const uint32_t zones = scene->gpuTime.count < 48u ? scene->gpuTime.count : 48u;
    for (uint32_t i = 0; i < zones; ++i) {
        gpu += scene->gpuTime.ms[i];
    }
    scene->gpuMs = gpu;
    if (scene->meshletSrc != nullptr && scene->instSrc != nullptr) {
        uint32_t tris = 0;
        for (uint32_t i = 0; i < scene->instanceCount; ++i) {
            const uint32_t mesh = scene->instSrc[i].meshId;
            if (mesh < scene->instanceCount) {
                tris += scene->meshletSrc[mesh].triangleCount;
            }
        }
        scene->sceneTris = tris;
    }
    sceneActorPointer(
        scene->actors,
        input.pointer.x,
        input.pointer.y,
        (input.pointer.down & kPointerLeft) != 0 ? 1 : 0,
        (input.pointer.pressed & kPointerLeft) != 0 ? 1 : 0,
        (input.pointer.released & kPointerLeft) != 0 ? 1 : 0);
    if (scene->physics != nullptr && scene->sim != 0) {
        const uint32_t n = physicsBodyCount(scene->physics);
        PhysicsPose poses[kPropCap]{};
        const uint32_t count = n < kPropCap ? n : kPropCap;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            const Xform pose = sceneActorXform(scene->actors, actor);
            poses[i].pos[0] = pose.pos[0];
            poses[i].pos[1] = pose.pos[1];
            poses[i].pos[2] = pose.pos[2];
            poses[i].euler[0] = pose.euler[0];
            poses[i].euler[1] = pose.euler[1];
            poses[i].euler[2] = pose.euler[2];
            poses[i].scale[0] = pose.scale[0];
            poses[i].scale[1] = pose.scale[1];
            poses[i].scale[2] = pose.scale[2];
        }
        const uint8_t held = scene->actors.drag <= 6 ? scene->actors.selected : kSceneActorNone;
        const float stepDt = scene->sim == 2 ? 0.0f : dt;
        const auto physicsBegan = std::chrono::steady_clock::now();
        physicsStep(scene->physics, stepDt, held, poses, count);
        scene->physicsMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - physicsBegan).count();
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            if (actor == held || actor >= scene->actors.count) {
                continue;
            }
            Xform pose = sceneActorXform(scene->actors, actor);
            pose.pos[0] = poses[i].pos[0];
            pose.pos[1] = poses[i].pos[1];
            pose.pos[2] = poses[i].pos[2];
            pose.euler[0] = poses[i].euler[0];
            pose.euler[1] = poses[i].euler[1];
            pose.euler[2] = poses[i].euler[2];
            pose.scale[0] = poses[i].scale[0];
            pose.scale[1] = poses[i].scale[1];
            pose.scale[2] = poses[i].scale[2];
            sceneActorWriteXform(scene->actors, actor, pose);
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            const float y = poses[i].pos[1];
            const float vy = dt > 1.0e-4f ? (y - scene->impactY[i]) / dt : 0.0f;
            if (actor != held && scene->impactVy[i] < -6.0f && vy > -0.8f) {
                particleBurst(scene->particles, poses[i].pos[0], poses[i].pos[1], poses[i].pos[2]);
            }
            scene->impactY[i] = y;
            scene->impactVy[i] = vy;
        }
    }
    const bool plan = scene->camFacing != 0 || scene->camOrtho != 0;
    InputFrame moved = input;
    if (plan && input.wheel != 0.0f) {
        moved.wheel = 0.0f;
        if (scene->camOrtho != 0) {
            scene->camZoom *= std::exp(-input.wheel * 0.08f);
            if (scene->camZoom < 0.05f) {
                scene->camZoom = 0.05f;
            }
            if (scene->camZoom > 32.0f) {
                scene->camZoom = 32.0f;
            }
        } else {
            float fx = 0.0f;
            float fy = 0.0f;
            float fz = 0.0f;
            flyCamForward(scene->fly, fx, fy, fz);
            const float dolly = -input.wheel * scene->fly.speed * 0.35f;
            scene->fly.eye[0] += fx * dolly;
            scene->fly.eye[1] += fy * dolly;
            scene->fly.eye[2] += fz * dolly;
        }
    }
    if (plan) {
        flyCamPan(scene->fly, moved, dt, scene->actors.right, scene->actors.up);
    } else {
        flyCamUpdate(scene->fly, moved, dt);
    }
    for (uint8_t i = 0; i < scene->actors.count; ++i) {
        flecs::entity ent = scene->actors.ent[i];
        if (!ent.is_alive() || !ent.has<LightLink>()) {
            continue;
        }
        const LightLink link = ent.get<LightLink>();
        if (link.index >= scene->importedCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene->actors, i);
        LightGpuData& lamp = scene->imported[link.index];
        lamp.pos[0] = pose.pos[0];
        lamp.pos[1] = pose.pos[1];
        lamp.pos[2] = pose.pos[2];
        if (lamp.type == kLightDirectional) {
            float fx = 0.0f;
            float fy = 0.0f;
            float fz = 0.0f;
            float bx[3]{};
            float by[3]{};
            float bz[3]{};
            actorBasis(pose.euler, bx, by, bz);
            fx = -by[0];
            fy = -by[1];
            fz = -by[2];
            lamp.direction[0] = fx;
            lamp.direction[1] = fy;
            lamp.direction[2] = fz;
            scene->world.sunDir[0] = fx;
            scene->world.sunDir[1] = fy;
            scene->world.sunDir[2] = fz;
        }
    }
    if (dt > 1.0e-4f) {
        scene->skyTime += dt;
    }
    scene->simDt = dt > 0.05f ? 0.05f : (dt < 0.0f ? 0.0f : dt);
    if (Weather* climate = scene->weatherEnt.try_get_mut<Weather>()) {
        weatherIntegrate(*climate, scene->simDt);
    }
    particleTick(scene->particles, scene->simDt);
    if (scene->particles.seed > scene->camBursts) {
        float kick = static_cast<float>(scene->particles.seed - scene->camBursts) * 0.08f;
        if (kick > 0.35f) {
            kick = 0.35f;
        }
        scene->camShock += kick;
        if (scene->camShock > 0.55f) {
            scene->camShock = 0.55f;
        }
        scene->camBursts = scene->particles.seed;
    }
    scene->camShockVel += -scene->camShock * 42.0f * scene->simDt;
    scene->camShockVel *= std::exp(-6.5f * scene->simDt);
    scene->camShock += scene->camShockVel * scene->simDt;
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = 1.0f;
    flyCamForward(scene->fly, fx, fy, fz);
    if (inputKeyEdge(input, 10)) {
        particleBurst(
            scene->particles,
            scene->fly.eye[0] + fx * 6.0f,
            scene->fly.eye[1] + fy * 6.0f,
            scene->fly.eye[2] + fz * 6.0f);
    }
    if (scene->audio != nullptr) {
        const float roof = physicsRoof(scene->physics, scene->fly.eye[0], scene->fly.eye[1], scene->fly.eye[2]) ? 1.0f : 0.0f;
        AudioListener ear{};
        ear.origin[0] = scene->fly.eye[0];
        ear.origin[1] = scene->fly.eye[1];
        ear.origin[2] = scene->fly.eye[2];
        ear.ahead[0] = fx;
        ear.ahead[1] = fy;
        ear.ahead[2] = fz;
        const float rx = -fz;
        const float ry = 0.0f;
        const float rz = fx;
        ear.up[0] = ry * fz - rz * fy;
        ear.up[1] = rz * fx - rx * fz;
        ear.up[2] = rx * fy - ry * fx;
        const float ul = std::sqrt(ear.up[0] * ear.up[0] + ear.up[1] * ear.up[1] + ear.up[2] * ear.up[2]);
        if (ul < 1.0e-4f) {
            ear.up[0] = 0.0f;
            ear.up[1] = 1.0f;
            ear.up[2] = 0.0f;
        } else {
            ear.up[0] /= ul;
            ear.up[1] /= ul;
            ear.up[2] /= ul;
        }
        audioSetListener(scene->audio, ear);
        audioSetRain(scene->audio, 1.0f - roof, scene->rainAmount);
        audioTick(scene->audio, scene->simDt);
    }
    if (scene->particles.intro == 0 && scene->skyTime > 0.6f) {
        scene->particles.intro = 1;
        particleBurst(
            scene->particles,
            scene->fly.eye[0] + fx * 8.0f,
            scene->fly.eye[1] + fy * 8.0f - 1.0f,
            scene->fly.eye[2] + fz * 8.0f);
    }
    return (input.pointer.down & kPointerRight) != 0;
}

void sceneFramePresented(SceneFrame* scene, float dt) {
    if (scene == nullptr || dt <= 1.0e-4f) {
        return;
    }
    scene->frameMs[scene->frameCursor % 128u] = dt * 1000.0f;
    scene->frameCursor += 1;
    const uint32_t filled = scene->frameCursor < 128u ? scene->frameCursor : 128u;
    const uint32_t recent = filled < 32u ? filled : 32u;
    double sum = 0.0;
    for (uint32_t i = 0; i < recent; ++i) {
        sum += static_cast<double>(scene->frameMs[(scene->frameCursor - 1u - i) % 128u]);
    }
    scene->fps = sum > 0.0 ? static_cast<float>((1000.0 * static_cast<double>(recent)) / sum) : 0.0f;
}

uint8_t sceneFrameActorCount(const SceneFrame* scene) {
    return scene != nullptr ? scene->actors.count : 0;
}

void sceneFrameActorName(const SceneFrame* scene, uint8_t index, char* name, uint8_t cap, uint8_t* kind) {
    if (name != nullptr && cap > 0) {
        name[0] = '\0';
    }
    if (kind != nullptr) {
        *kind = 0;
    }
    if (scene == nullptr || index >= scene->actors.count || name == nullptr || cap == 0) {
        return;
    }
    const char* src = actorNameOf(scene->actors, index);
    if (actorXformOf(scene->actors, index) == nullptr) {
        return;
    }
    uint8_t n = 0;
    while (src[n] != '\0' && n + 1 < cap) {
        name[n] = src[n];
        ++n;
    }
    name[n] = '\0';
    if (kind != nullptr) {
        *kind = actorKindOf(scene->actors, index);
    }
}

void sceneFrameActorSelect(SceneFrame* scene, uint8_t index) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.selected = index < scene->actors.count ? index : kSceneActorNone;
    scene->actors.drag = kSceneActorNone;
}

uint8_t sceneFrameActorSelected(const SceneFrame* scene) {
    return scene != nullptr ? scene->actors.selected : kSceneActorNone;
}

uint8_t sceneFrameActorKind(const SceneFrame* scene, uint8_t index) {
    if (scene == nullptr || index >= scene->actors.count) {
        return 0;
    }
    return actorXformOf(scene->actors, index) != nullptr ? actorKindOf(scene->actors, index) : static_cast<uint8_t>(0);
}

void sceneFrameActorGizmo(SceneFrame* scene, uint8_t mode) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.tool = mode > 4 ? 0 : mode;
    scene->actors.gizmo = scene->actors.tool;
    scene->actors.drag = kSceneActorNone;
    sceneActorFitTool(scene->actors);
}

void sceneFrameGizmoSpace(SceneFrame* scene, uint8_t worldSpace) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.space = worldSpace == 0 ? 0 : 1;
    scene->actors.drag = kSceneActorNone;
}

void sceneFrameSnap(SceneFrame* scene, uint8_t channel, uint8_t enabled, float step) {
    if (scene == nullptr || channel > 2) {
        return;
    }
    scene->actors.snapOn[channel] = enabled != 0 ? 1 : 0;
    if (step > 1.0e-5f) {
        scene->actors.snapStep[channel] = step;
    }
}

static float clampRange(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void sceneFrameCamera(SceneFrame* scene, float fovDeg, float nearZ, float farZ) {
    if (scene == nullptr) {
        return;
    }
    fovDeg = clampRange(fovDeg, 10.0f, 120.0f);
    nearZ = clampRange(nearZ, 0.01f, 10.0f);
    farZ = clampRange(farZ, 100.0f, 100000.0f);
    if (nearZ >= farZ * 0.5f) {
        nearZ = farZ * 0.01f;
    }
    scene->fovYRad = fovDeg * 0.017453292f;
    scene->nearPlane = nearZ;
    scene->farPlane = farZ;
}

void sceneFrameCameraSpeed(SceneFrame* scene, float speed) {
    if (scene == nullptr) {
        return;
    }
    scene->fly.speed = clampRange(speed, 0.05f, 200.0f);
}

void sceneFrameCameraView(SceneFrame* scene, uint8_t view) {
    if (scene == nullptr) {
        return;
    }
    if (view > 6) {
        view = 0;
    }
    if (view == scene->camFacing) {
        return;
    }
    if (view == 0) {
        scene->camFacing = 0;
        scene->camView = scene->camOrtho;
        return;
    }
    float fx = 0.0f;
    float fy = 0.0f;
    float fz = 0.0f;
    flyCamForward(scene->fly, fx, fy, fz);
    constexpr float kDist = 14.0f;
    const float pivot[3] = {
        scene->fly.eye[0] + fx * kDist,
        scene->fly.eye[1] + fy * kDist,
        scene->fly.eye[2] + fz * kDist,
    };
    float yaw = 0.0f;
    float pitch = 0.0f;
    const uint8_t axis = static_cast<uint8_t>(view - 1);
    if (axis == 0) {
        pitch = -1.55f;
    } else if (axis == 1) {
        pitch = 1.55f;
    } else if (axis == 2) {
        yaw = -1.5707963f;
    } else if (axis == 3) {
        yaw = 1.5707963f;
    } else if (axis == 5) {
        yaw = 3.14159265f;
    }
    scene->fly.yaw = yaw;
    scene->fly.pitch = pitch;
    flyCamForward(scene->fly, fx, fy, fz);
    scene->fly.eye[0] = pivot[0] - fx * kDist;
    scene->fly.eye[1] = pivot[1] - fy * kDist;
    scene->fly.eye[2] = pivot[2] - fz * kDist;
    scene->camFacing = view;
    scene->camView = scene->camOrtho;
}

void sceneFrameCameraProject(SceneFrame* scene, uint8_t ortho) {
    if (scene == nullptr) {
        return;
    }
    scene->camOrtho = ortho != 0 ? 1 : 0;
    scene->camView = scene->camOrtho;
}

uint8_t sceneFrameCameraProject(const SceneFrame* scene) {
    return scene != nullptr && scene->camOrtho != 0 ? 1 : 0;
}

void sceneFrameCameraRelease(SceneFrame* scene) {
    if (scene == nullptr || scene->camFacing == 0) {
        return;
    }
    scene->camFacing = 0;
    scene->camView = scene->camOrtho;
}

uint8_t sceneFrameCameraMode(const SceneFrame* scene) {
    return scene != nullptr ? scene->camFacing : 0;
}

void sceneFrameGizmoPivot(SceneFrame* scene, uint8_t center) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.pivotSpace = center != 0 ? 1 : 0;
    scene->actors.drag = kSceneActorNone;
}

bool sceneFrameActorPivot(const SceneFrame* scene, uint8_t index, float out[3]) {
    if (scene == nullptr || out == nullptr || index >= scene->actors.count) {
        return false;
    }
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    const SceneActors& world = scene->actors;
    if (world.ent[index].is_alive() && world.ent[index].has<Pivot>()) {
        const Pivot& pivot = world.ent[index].get<Pivot>();
        out[0] = pivot.local[0];
        out[1] = pivot.local[1];
        out[2] = pivot.local[2];
    }
    return true;
}

void sceneFrameActorSetPivot(SceneFrame* scene, uint8_t index, const float local[3]) {
    if (scene == nullptr || local == nullptr) {
        return;
    }
    actorWritePivot(scene->actors, index, local);
}

float sceneFrameCameraFov(const SceneFrame* scene) {
    return scene != nullptr ? scene->fovYRad * 57.2957795f : 60.0f;
}

float sceneFrameCameraNear(const SceneFrame* scene) {
    return scene != nullptr ? scene->nearPlane : 0.1f;
}

float sceneFrameCameraFar(const SceneFrame* scene) {
    return scene != nullptr ? scene->farPlane : 8000.0f;
}

float sceneFrameCameraSpeed(const SceneFrame* scene) {
    return scene != nullptr ? scene->fly.speed : 4.0f;
}

uint8_t sceneFrameActorDuplicate(SceneFrame* scene) {
    if (scene == nullptr) {
        return kSceneActorNone;
    }
    return sceneActorDuplicate(scene->actors);
}

bool sceneFrameActorDragging(const SceneFrame* scene) {
    return scene != nullptr && scene->actors.drag <= 6;
}

void sceneFrameActorEuler(const SceneFrame* scene, uint8_t index, float& pitch, float& yaw) {
    pitch = 35.0f;
    yaw = 30.0f;
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    const Xform pose = sceneActorXform(scene->actors, index);
    pitch = pose.euler[0];
    yaw = pose.euler[1];
}

void sceneFrameActorSetEuler(SceneFrame* scene, uint8_t index, float pitch, float yaw) {
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    Xform pose = sceneActorXform(scene->actors, index);
    pose.euler[0] = pitch;
    pose.euler[1] = yaw;
    sceneActorWriteXform(scene->actors, index, pose);
}

uint8_t sceneFrameActorSections(const SceneFrame* scene, uint8_t index) {
    uint8_t mask = 0;
    if (scene == nullptr || index >= scene->actors.count) {
        return mask;
    }
    const flecs::entity ent = scene->actors.ent[index];
    if (!ent.is_alive()) {
        return mask;
    }
    if (ent.has<Transform>()) {
        mask |= kSecTransform;
    }
    if (ent.has<Material>()) {
        mask |= kSecMaterial;
    }
    if (ent.has<PostProcess>()) {
        mask |= kSecPost;
    }
    if (ent.has<Atmosphere>()) {
        mask |= kSecAtmo;
    }
    if (ent.has<WeatherObj>()) {
        return kSecWeather;
    }
    if (ent.has<SunObj>()) {
        mask |= kSecSun;
    }
    return mask;
}

void sceneFrameWeatherPreset(SceneFrame* scene, uint32_t preset) {
    if (scene == nullptr) {
        return;
    }
    if (Weather* climate = scene->weatherEnt.try_get_mut<Weather>()) {
        weatherPreset(*climate, preset);
    }
}

float sceneFrameWeatherTime(const SceneFrame* scene) {
    if (scene == nullptr) {
        return -1.0f;
    }
    const Weather* climate = scene->weatherEnt.try_get<Weather>();
    return climate != nullptr ? climate->timeOfDay : -1.0f;
}

float sceneFrameWeatherZenith(const SceneFrame* scene) {
    if (scene == nullptr) {
        return 35.0f;
    }
    const Weather* climate = scene->weatherEnt.try_get<Weather>();
    return climate != nullptr ? climate->sunZenith : 35.0f;
}

float sceneFrameWeatherAzimuth(const SceneFrame* scene) {
    if (scene == nullptr) {
        return 30.0f;
    }
    const Weather* climate = scene->weatherEnt.try_get<Weather>();
    return climate != nullptr ? climate->sunAzimuth : 30.0f;
}

bool sceneFrameActorPose(const SceneFrame* scene, uint8_t index, ActorPose& out) {
    if (scene == nullptr || index >= scene->actors.count) {
        return false;
    }
    const Xform pose = sceneActorXform(scene->actors, index);
    out.pos[0] = pose.pos[0];
    out.pos[1] = pose.pos[1];
    out.pos[2] = pose.pos[2];
    out.euler[0] = pose.euler[0];
    out.euler[1] = pose.euler[1];
    out.euler[2] = pose.euler[2];
    out.scale[0] = pose.scale[0];
    out.scale[1] = pose.scale[1];
    out.scale[2] = pose.scale[2];
    out.isStatic = 0;
    const flecs::entity ent = scene->actors.ent[index];
    if (ent.is_alive() && ent.has<Transform>()) {
        out.isStatic = ent.get<Transform>().isStatic;
    }
    return true;
}

void sceneFrameActorSetPose(SceneFrame* scene, uint8_t index, const ActorPose& pose) {
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    Xform next = sceneActorXform(scene->actors, index);
    next.pos[0] = pose.pos[0];
    next.pos[1] = pose.pos[1];
    next.pos[2] = pose.pos[2];
    next.euler[0] = pose.euler[0];
    next.euler[1] = pose.euler[1];
    next.euler[2] = pose.euler[2];
    next.scale[0] = pose.scale[0] < 0.05f ? 0.05f : pose.scale[0];
    next.scale[1] = pose.scale[1] < 0.05f ? 0.05f : pose.scale[1];
    next.scale[2] = pose.scale[2] < 0.05f ? 0.05f : pose.scale[2];
    sceneActorWriteXform(scene->actors, index, next);
    flecs::entity ent = scene->actors.ent[index];
    if (ent.is_alive() && ent.has<Transform>()) {
        ent.get_mut<Transform>().isStatic = pose.isStatic;
    }
}

bool sceneFrameActorPbr(const SceneFrame* scene, uint8_t index, ActorPbr& out) {
    if (scene == nullptr || index >= scene->actors.count) {
        return false;
    }
    flecs::entity ent = scene->actors.ent[index];
    if (!ent.is_alive() || !ent.has<Material>()) {
        return false;
    }
    const Material& mat = ent.get<Material>();
    out.base[0] = mat.base[0];
    out.base[1] = mat.base[1];
    out.base[2] = mat.base[2];
    out.roughness = mat.roughness;
    out.metallic = mat.metallic;
    out.emissive = mat.emissive;
    return true;
}

void sceneFrameActorSetPbr(SceneFrame* scene, uint8_t index, const ActorPbr& pbr) {
    if (scene == nullptr || index >= scene->actors.count) {
        return;
    }
    flecs::entity ent = scene->actors.ent[index];
    if (!ent.is_alive()) {
        return;
    }
    Material mat{};
    if (ent.has<Material>()) {
        mat = ent.get<Material>();
    }
    mat.base[0] = pbr.base[0];
    mat.base[1] = pbr.base[1];
    mat.base[2] = pbr.base[2];
    mat.base[3] = 1.0f;
    mat.roughness = pbr.roughness;
    mat.metallic = pbr.metallic;
    mat.emissive = pbr.emissive;
    ent.set<Material>(mat);
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

namespace {

void captureActors(SceneFrameImpl& scene) {
    scene.snapId.clear();
    scene.snapActor.clear();
    scene.snapSize.clear();
    scene.snapBytes.clear();
    ecs_world_t* world = scene.actors.world.c_ptr();
    for (uint8_t actor = 0; actor < scene.actors.count; ++actor) {
        flecs::entity ent = scene.actors.ent[actor];
        if (!ent.is_alive()) {
            continue;
        }
        const ecs_type_t* type = ecs_get_type(world, ent.id());
        if (type == nullptr) {
            continue;
        }
        for (int32_t i = 0; i < type->count; ++i) {
            const ecs_id_t id = type->array[i];
            const ecs_type_info_t* info = ecs_get_type_info(world, id);
            if (info == nullptr || info->size <= 0 || info->size > 512) {
                continue;
            }
            const void* src = ecs_get_id(world, ent.id(), id);
            if (src == nullptr) {
                continue;
            }
            const uint32_t at = static_cast<uint32_t>(scene.snapBytes.size());
            scene.snapBytes.resize(at + static_cast<uint32_t>(info->size));
            std::memcpy(scene.snapBytes.data() + at, src, static_cast<size_t>(info->size));
            scene.snapId.push_back(id);
            scene.snapActor.push_back(actor);
            scene.snapSize.push_back(static_cast<uint16_t>(info->size));
        }
    }
}

void restoreActors(SceneFrameImpl& scene) {
    ecs_world_t* world = scene.actors.world.c_ptr();
    uint32_t at = 0;
    for (size_t i = 0; i < scene.snapId.size(); ++i) {
        const uint16_t actor = scene.snapActor[i];
        const uint16_t size = scene.snapSize[i];
        if (actor >= scene.actors.count || at + size > scene.snapBytes.size()) {
            break;
        }
        flecs::entity ent = scene.actors.ent[actor];
        if (ent.is_alive()) {
            ecs_set_id(world, ent.id(), scene.snapId[i], size, scene.snapBytes.data() + at);
        }
        at += size;
    }
}

} // namespace

void sceneFramePlay(SceneFrame* scene) {
    if (scene == nullptr || scene->sim != 0) {
        return;
    }
    captureActors(*scene);
    scene->sim = 1;
}

void sceneFrameStop(SceneFrame* scene) {
    if (scene == nullptr || scene->sim == 0) {
        return;
    }
    restoreActors(*scene);
    scene->sim = 1;
    if (scene->physics != nullptr) {
        const uint32_t n = physicsBodyCount(scene->physics);
        PhysicsPose poses[kPropCap]{};
        const uint32_t count = n < kPropCap ? n : kPropCap;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t actor = physicsBodyActor(scene->physics, i);
            const Xform pose = sceneActorXform(scene->actors, actor);
            poses[i].pos[0] = pose.pos[0];
            poses[i].pos[1] = pose.pos[1];
            poses[i].pos[2] = pose.pos[2];
            poses[i].euler[0] = pose.euler[0];
            poses[i].euler[1] = pose.euler[1];
            poses[i].euler[2] = pose.euler[2];
            poses[i].scale[0] = pose.scale[0];
            poses[i].scale[1] = pose.scale[1];
            poses[i].scale[2] = pose.scale[2];
        }
        physicsStep(scene->physics, 0.0f, kSceneActorNone, poses, count);
    }
    scene->sim = 0;
}

void sceneFramePause(SceneFrame* scene, uint8_t paused) {
    if (scene == nullptr || scene->sim == 0) {
        return;
    }
    scene->sim = paused != 0 ? 2 : 1;
}

uint8_t sceneFrameSim(const SceneFrame* scene) {
    return scene != nullptr ? scene->sim : 0;
}

float sceneFrameFps(const SceneFrame* scene) {
    return scene != nullptr ? scene->fps : 0.0f;
}

float sceneFramePhysicsMs(const SceneFrame* scene) {
    return scene != nullptr ? scene->physicsMs : 0.0f;
}

float sceneFrameGpuMs(const SceneFrame* scene) {
    return scene != nullptr ? scene->gpuMs : 0.0f;
}

uint32_t sceneFrameTriangles(const SceneFrame* scene) {
    return scene != nullptr ? scene->sceneTris : 0;
}

uint32_t sceneFrameDraws(const SceneFrame* scene) {
    return scene != nullptr ? scene->visibleCount : 0;
}

const char* sceneFrameTitle(const SceneFrame* scene) {
    return scene != nullptr ? scene->title : "";
}

void sceneFrameSetDecals(SceneFrame* scene, const DecalGpu* decals, uint32_t count) {
    if (scene == nullptr) {
        return;
    }
    scene->decalCount = 0;
    if (decals == nullptr || count == 0 || scene->decalBuf.mapped == nullptr) {
        return;
    }
    if (count > kDecalCap) {
        count = kDecalCap;
    }
    std::memcpy(scene->decals, decals, sizeof(DecalGpu) * count);
    std::memcpy(scene->decalBuf.mapped, scene->decals, sizeof(DecalGpu) * count);
    scene->decalCount = count;
}

void sceneFrameSetSectors(SceneFrame* scene, const Sector* sectors, uint32_t sectorCount, const Portal* portals, uint32_t portalCount) {
    if (scene == nullptr) {
        return;
    }
    scene->sectorCount = 0;
    scene->portalCount = 0;
    if (sectors != nullptr && sectorCount > 0) {
        if (sectorCount > 32u) {
            sectorCount = 32u;
        }
        std::memcpy(scene->sectors, sectors, sizeof(Sector) * sectorCount);
        scene->sectorCount = sectorCount;
    }
    if (portals != nullptr && portalCount > 0) {
        if (portalCount > 64u) {
            portalCount = 64u;
        }
        std::memcpy(scene->portals, portals, sizeof(Portal) * portalCount);
        scene->portalCount = portalCount;
    }
}

void sceneFrameDebug(SceneFrame* scene, bool hiz, bool cone, bool lod, bool frustum, bool subpixel, bool shadow, bool backface, bool screen, bool behind, uint32_t mode) {
    if (scene == nullptr) {
        return;
    }
    if (mode > 19u) {
        mode = 0;
    }
    if (scene->showHiz == hiz && scene->showCone == cone
        && scene->showLod == lod && scene->showFrustum == frustum && scene->showSubpixel == subpixel
        && scene->showShadow == shadow && scene->showBackface == backface
        && scene->showScreen == screen && scene->showBehind == behind
        && scene->debugMode == mode) {
        return;
    }
    scene->showHiz = hiz;
    scene->showCone = cone;
    scene->showLod = lod;
    scene->showFrustum = frustum;
    scene->showSubpixel = subpixel;
    scene->showShadow = shadow;
    scene->showBackface = backface;
    scene->showScreen = screen;
    scene->showBehind = behind;
    scene->debugMode = mode;
    spdlog::info("debug profile {} hiz {} cone {} lod {} frust {} small {} shadow {} mode {}",
        scene->lightProfile, hiz ? 1 : 0, cone ? 1 : 0, lod ? 1 : 0, frustum ? 1 : 0, subpixel ? 1 : 0, shadow ? 1 : 0, mode);
}

void sceneFrameStats(const SceneFrame* scene, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (scene == nullptr) {
        return;
    }
    const uint32_t n = scene->frameCursor < 128u ? scene->frameCursor : 128u;
    float copy[128];
    float worst = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        copy[i] = scene->frameMs[i];
        if (copy[i] > worst) {
            worst = copy[i];
        }
    }
    for (uint32_t i = 1; i < n; ++i) {
        const float key = copy[i];
        uint32_t j = i;
        while (j > 0 && copy[j - 1] < key) {
            copy[j] = copy[j - 1];
            j -= 1;
        }
        copy[j] = key;
    }
    const float low = worst > 0.0f ? 1000.0f / worst : scene->fps;
    uint32_t slowN = n / 100u;
    if (slowN < 1u && n > 0u) {
        slowN = 1u;
    }
    float slowSum = 0.0f;
    for (uint32_t i = 0; i < slowN; ++i) {
        slowSum += copy[i];
    }
    const float one = slowSum > 0.0f ? (1000.0f * static_cast<float>(slowN)) / slowSum : scene->fps;
    std::snprintf(out, cap, "fps %.0f   low %.0f   1%% %.0f   mesh %u/%u", scene->fps, low, one, scene->visibleCount, scene->instanceCount);
}

void sceneFrameKick(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    RadiancePass& rad = scene->radiance;
    const bool loaded = scene->kickHeaps != nullptr && scene->vertSrc != nullptr;
    if (loaded && !rad.geometry && rad.buildTask.stage == 0) {
        uint32_t meshCount = scene->instanceCount;
        if (scene->meshletSrc == scene->meshletGpu.data()) {
            meshCount = static_cast<uint32_t>(scene->meshletGpu.size());
        } else if (scene->meshletSrc == scene->ownedMesh.data()) {
            meshCount = static_cast<uint32_t>(scene->ownedMesh.size());
        }
        radianceArm(rad, device, *scene->kickHeaps, scene->vertSrc, scene->indexSrc, scene->instSrc, scene->meshletSrc,
            scene->instanceCount, scene->vertCount, scene->indexCount, meshCount);
    }
    radianceKick(rad, device);
}

void syncMeshActors(SceneFrameImpl& scene) {
    if (scene.instSrc == nullptr || scene.instanceCount == 0) {
        return;
    }
    bool moved = false;
    for (uint8_t n = 0; n < scene.actors.count; ++n) {
        flecs::entity ent = scene.actors.ent[n];
        if (!ent.is_alive() || !ent.has<MeshBind>()) {
            continue;
        }
        const MeshBind bind = ent.get<MeshBind>();
        if (bind.instance >= scene.instanceCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene.actors, n);
        const GpuInstance& inst = scene.instSrc[bind.instance];
        const float wantX = pose.pos[0] - bind.anchor[0];
        const float wantY = pose.pos[1] - bind.anchor[1];
        const float wantZ = pose.pos[2] - bind.anchor[2];
        const float dx = wantX - inst.world[3];
        const float dy = wantY - inst.world[7];
        const float dz = wantZ - inst.world[11];
        if (dx * dx + dy * dy + dz * dz > 1.0e-4f) {
            moved = true;
            break;
        }
    }
    if (!moved) {
        return;
    }
    if (scene.instSrc != scene.ownedInst.data()) {
        scene.ownedInst.assign(scene.instSrc, scene.instSrc + scene.instanceCount);
        scene.instSrc = scene.ownedInst.data();
    }
    auto* gpu = scene.instances.mapped != nullptr ? static_cast<GpuInstance*>(scene.instances.mapped) : nullptr;
    for (uint8_t n = 0; n < scene.actors.count; ++n) {
        flecs::entity ent = scene.actors.ent[n];
        if (!ent.is_alive() || !ent.has<MeshBind>()) {
            continue;
        }
        const MeshBind bind = ent.get<MeshBind>();
        if (bind.instance >= scene.instanceCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene.actors, n);
        GpuInstance& inst = scene.ownedInst[bind.instance];
        const float wantX = pose.pos[0] - bind.anchor[0];
        const float wantY = pose.pos[1] - bind.anchor[1];
        const float wantZ = pose.pos[2] - bind.anchor[2];
        const float dx = wantX - inst.world[3];
        const float dy = wantY - inst.world[7];
        const float dz = wantZ - inst.world[11];
        if (ent.has<MeshSpan>()) {
            const MeshSpan span = ent.get<MeshSpan>();
            const uint32_t end = span.begin + span.count;
            for (uint32_t i = span.begin; i < end && i < scene.instanceCount; ++i) {
                GpuInstance& part = scene.ownedInst[i];
                part.sphere[0] += dx;
                part.sphere[1] += dy;
                part.sphere[2] += dz;
                part.world[3] += dx;
                part.world[7] += dy;
                part.world[11] += dz;
                if (gpu != nullptr) {
                    gpu[i] = part;
                }
            }
            continue;
        }
        inst.sphere[0] += dx;
        inst.sphere[1] += dy;
        inst.sphere[2] += dz;
        inst.world[3] = wantX;
        inst.world[7] = wantY;
        inst.world[11] = wantZ;
        if (gpu != nullptr) {
            gpu[bind.instance] = inst;
        }
    }
}

bool syncPropInstance(SceneFrame* scene) {
    syncMeshActors(*scene);
    GpuInstance* table = nullptr;
    if (scene->instSrc == scene->cpuInstances) {
        table = scene->cpuInstances;
    } else if (!scene->ownedInst.empty() && scene->instSrc == scene->ownedInst.data()) {
        table = scene->ownedInst.data();
    }
    if (scene->propCount == 0 || table == nullptr) {
        return false;
    }
    bool moved = false;
    for (uint8_t n = 0; n < scene->propCount; ++n) {
        SceneFrameImpl::PropLink& link = scene->props[n];
        if (link.actor == 0xff || link.instance >= scene->instanceCount) {
            continue;
        }
        const Xform pose = sceneActorXform(scene->actors, link.actor);
        const Xform& prev = link.pose;
        const float dp = (pose.pos[0] - prev.pos[0]) * (pose.pos[0] - prev.pos[0])
            + (pose.pos[1] - prev.pos[1]) * (pose.pos[1] - prev.pos[1])
            + (pose.pos[2] - prev.pos[2]) * (pose.pos[2] - prev.pos[2]);
        const float de = (pose.euler[0] - prev.euler[0]) * (pose.euler[0] - prev.euler[0])
            + (pose.euler[1] - prev.euler[1]) * (pose.euler[1] - prev.euler[1])
            + (pose.euler[2] - prev.euler[2]) * (pose.euler[2] - prev.euler[2]);
        const float ds = (pose.scale[0] - prev.scale[0]) * (pose.scale[0] - prev.scale[0])
            + (pose.scale[1] - prev.scale[1]) * (pose.scale[1] - prev.scale[1])
            + (pose.scale[2] - prev.scale[2]) * (pose.scale[2] - prev.scale[2]);
        if (dp < 1.0e-8f && de < 1.0e-6f && ds < 1.0e-8f) {
            continue;
        }
        float axisX[3]{};
        float axisY[3]{};
        float axisZ[3]{};
        actorBasis(pose.euler, axisX, axisY, axisZ);
        GpuInstance& cpu = table[link.instance];
        cpu.world[0] = axisX[0] * pose.scale[0];
        cpu.world[4] = axisX[1] * pose.scale[0];
        cpu.world[8] = axisX[2] * pose.scale[0];
        cpu.world[1] = axisY[0] * pose.scale[1];
        cpu.world[5] = axisY[1] * pose.scale[1];
        cpu.world[9] = axisY[2] * pose.scale[1];
        cpu.world[2] = axisZ[0] * pose.scale[2];
        cpu.world[6] = axisZ[1] * pose.scale[2];
        cpu.world[10] = axisZ[2] * pose.scale[2];
        cpu.world[3] = pose.pos[0];
        cpu.world[7] = pose.pos[1];
        cpu.world[11] = pose.pos[2];
        cpu.sphere[0] = pose.pos[0];
        cpu.sphere[1] = pose.pos[1];
        cpu.sphere[2] = pose.pos[2];
        const float smax = std::max(pose.scale[0], std::max(pose.scale[1], pose.scale[2]));
        cpu.sphere[3] = link.radius * std::max(smax, 0.001f);
        if (scene->instances.mapped != nullptr) {
            auto* gpu = static_cast<GpuInstance*>(scene->instances.mapped);
            gpu[link.instance] = cpu;
        }
        link.pose = pose;
        moved = true;
    }
    return moved;
}

void syncPropMaterial(SceneFrame& scene) {
    auto* gpu = scene.materialsBuf.mapped != nullptr ? static_cast<MaterialGpuData*>(scene.materialsBuf.mapped) : nullptr;
    auto store = [](MaterialGpuData& dst, const Material& src) {
        if (dst.baseColor[0] == src.base[0] && dst.baseColor[1] == src.base[1] && dst.baseColor[2] == src.base[2]
            && dst.roughness == src.roughness && dst.metallic == src.metallic && dst.emissiveIntensity == src.emissive) {
            return false;
        }
        dst.baseColor[0] = src.base[0];
        dst.baseColor[1] = src.base[1];
        dst.baseColor[2] = src.base[2];
        dst.roughness = src.roughness;
        dst.metallic = src.metallic;
        dst.emissiveIntensity = src.emissive;
        return true;
    };
    for (uint8_t n = 0; n < scene.propCount; ++n) {
        SceneFrameImpl::PropLink& link = scene.props[n];
        if (link.actor >= scene.actors.count || link.material >= scene.materialCount) {
            continue;
        }
        flecs::entity ent = scene.actors.ent[link.actor];
        if (!ent.is_alive() || !ent.has<Material>()) {
            continue;
        }
        const Material& pbr = ent.get<Material>();
        bool dirty = false;
        if (MaterialGpuData* cpu = materialAt(scene, link.material)) {
            if (store(*cpu, pbr)) {
                dirty = true;
            }
        }
        if (gpu != nullptr && store(gpu[link.material], pbr)) {
            dirty = true;
        }
        if (dirty) {
            scene.editorDirty = static_cast<uint8_t>(scene.editorDirty | 1u);
        }
    }
    const uint8_t actors = scene.actors.count;
    for (uint8_t n = 0; n < actors; ++n) {
        flecs::entity ent = scene.actors.ent[n];
        if (!ent.is_alive() || !ent.has<MeshBind>() || !ent.has<Material>()) {
            continue;
        }
        const MeshBind bind = ent.get<MeshBind>();
        if (bind.material >= scene.materialCount) {
            continue;
        }
        const Material& pbr = ent.get<Material>();
        bool dirty = false;
        if (MaterialGpuData* cpu = materialAt(scene, bind.material)) {
            if (store(*cpu, pbr)) {
                dirty = true;
            }
        }
        if (gpu != nullptr && store(gpu[bind.material], pbr)) {
            dirty = true;
        }
        if (dirty) {
            scene.editorDirty = static_cast<uint8_t>(scene.editorDirty | 1u);
        }
    }
}

void followHandLamp(SceneFrameImpl& s) {
    s.handOn = false;
}

bool ensureOwnedMaterials(SceneFrameImpl& s) {
    if (s.materialCount == 0 || s.materialSrc == nullptr) {
        return false;
    }
    if (!s.ownedMat.empty() && s.materialSrc == s.ownedMat.data()) {
        return true;
    }
    s.ownedMat.assign(s.materialSrc, s.materialSrc + s.materialCount);
    s.ownedMat.reserve(s.materialCount + kEditorSpare);
    s.materialSrc = s.ownedMat.data();
    if (s.materialCap < s.materialCount + kEditorSpare) {
        s.materialCap = s.materialCount + kEditorSpare;
    }
    return true;
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

bool sceneFrameCloneMaterial(SceneFrame* scene, uint8_t actor) {
    if (scene == nullptr || actor >= scene->actors.count) {
        return false;
    }
    flecs::entity ent = scene->actors.ent[actor];
    if (!ent.is_alive() || !ent.has<MeshBind>()) {
        spdlog::error("clone material: actor {} has no mesh", actor);
        return false;
    }
    if (!ensureOwnedMaterials(*scene) || scene->materialCount >= scene->materialCap) {
        spdlog::error("clone material: no spare slot {} / {}", scene->materialCount, scene->materialCap);
        return false;
    }
    const MeshBind bind = ent.get<MeshBind>();
    if (bind.material >= scene->materialCount) {
        return false;
    }
    const uint32_t id = scene->materialCount;
    scene->ownedMat.push_back(scene->ownedMat[bind.material]);
    scene->materialSrc = scene->ownedMat.data();
    scene->materialCount = id + 1u;
    if (scene->materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene->materialsBuf.mapped)[id] = scene->ownedMat[id];
    }
    if (!ensureOwnedInstances(*scene)) {
        return false;
    }
    MeshSpan span{};
    span.begin = bind.instance;
    span.count = 1;
    if (ent.has<MeshSpan>()) {
        span = ent.get<MeshSpan>();
    }
    auto* gpu = scene->instances.mapped != nullptr ? static_cast<GpuInstance*>(scene->instances.mapped) : nullptr;
    const uint32_t end = span.begin + span.count;
    for (uint32_t i = span.begin; i < end && i < scene->instanceCount; ++i) {
        scene->ownedInst[i].materialId = id;
        if (gpu != nullptr) {
            gpu[i].materialId = id;
        }
    }
    MeshBind next = bind;
    next.material = id;
    ent.set<MeshBind>(next);
    scene->editorDirty = static_cast<uint8_t>(scene->editorDirty | 1u);
    spdlog::info("material {} cloned from {} for actor {}", id, bind.material, actor);
    return true;
}

void sceneFrameQueueTexture(SceneFrame* scene, uint8_t actor, uint8_t channel, const char* path) {
    if (scene == nullptr || path == nullptr || path[0] == '\0' || channel > 7u || scene->texJobN >= 8u) {
        return;
    }
    SceneFrameImpl::TexJob& job = scene->texJob[scene->texJobN];
    std::snprintf(job.path, sizeof(job.path), "%s", path);
    job.actor = actor;
    job.channel = channel;
    scene->texJobN = static_cast<uint8_t>(scene->texJobN + 1u);
    scene->texPath[0] = '\0';
    scene->texChannel = 0xff;
    spdlog::info("texture queued ch {} actor {} {}", channel, actor, path);
}

uint32_t actorMaterialSlot(const SceneFrame& scene, uint8_t actor) {
    if (actor >= scene.actors.count) {
        return 0xffffffffu;
    }
    flecs::entity ent = scene.actors.ent[actor];
    if (ent.is_alive() && ent.has<MeshBind>()) {
        return ent.get<MeshBind>().material;
    }
    for (uint8_t n = 0; n < scene.propCount; ++n) {
        if (scene.props[n].actor == actor) {
            return scene.props[n].material;
        }
    }
    return 0xffffffffu;
}

void queueDocMap(SceneFrame& scene, uint8_t actor, uint8_t channel, const char* path, const char* previous) {
    if (path != nullptr && path[0] != '\0') {
        if (previous != nullptr && std::strcmp(path, previous) == 0) {
            return;
        }
        sceneFrameQueueTexture(&scene, actor, channel, path);
        return;
    }
    const uint32_t id = actorMaterialSlot(scene, actor);
    if (id >= scene.materialCount || id >= scene.ownedMat.size()) {
        return;
    }
    MaterialGpuData& mat = scene.ownedMat[id];
    uint32_t* slot = &mat.albedoTexIndex;
    uint32_t none = 0xffffffffu;
    if (channel == 1u) {
        slot = &mat.normalTexIndex;
        none = 0;
    } else if (channel == 2u) {
        slot = &mat.ormTexIndex;
        none = 0;
    } else if (channel == 3u) {
        slot = &mat.emissiveTexIndex;
        none = 0;
    } else if (channel == 4u) {
        slot = &mat.pad[2];
        none = 0;
    } else if (channel == 5u) {
        slot = &mat.pad[3];
        none = 0;
    } else if (channel == 6u) {
        slot = &mat.aoTex;
        none = 0;
    } else if (channel == 7u) {
        slot = &mat.pad[0];
        none = 0;
    } else if (channel == 8u) {
        slot = &mat.tail[1];
        none = 0;
    }
    if (*slot == none && (channel != 2u || (mat.flags & kMatOrm) == 0u)) {
        return;
    }
    *slot = none;
    if (channel == 2u) {
        mat.flags &= ~kMatOrm;
    }
    if (scene.materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene.materialsBuf.mapped)[id] = mat;
    }
}

bool sceneFrameApplyDoc(SceneFrame* scene, uint8_t actor, const BhmatDoc& doc) {
    if (scene == nullptr || actor >= scene->actors.count || !ensureOwnedMaterials(*scene)) {
        return false;
    }
    const uint32_t id = actorMaterialSlot(*scene, actor);
    if (id >= scene->materialCount || id >= scene->ownedMat.size()) {
        return false;
    }
    BhmatDoc previous{};
    flecs::entity ent = scene->actors.ent[actor];
    if (ent.is_alive() && ent.has<ActorMat>()) {
        previous = ent.get<ActorMat>().doc;
    }
    MaterialGpuData& mat = scene->ownedMat[id];
    materialGpuFromDoc(mat, doc);
    if (scene->materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene->materialsBuf.mapped)[id] = mat;
    }
    if (ent.is_alive()) {
        ActorMat stored{};
        stored.doc = doc;
        ent.set<ActorMat>(stored);
        Material pbr{};
        pbr.base[0] = doc.base[0];
        pbr.base[1] = doc.base[1];
        pbr.base[2] = doc.base[2];
        pbr.base[3] = doc.baseA;
        pbr.roughness = doc.roughness;
        pbr.metallic = doc.metallic;
        pbr.emissive = doc.emissiveGain > 0.0f ? doc.emissiveGain : doc.emissive;
        pbr.reflectance = doc.reflectance;
        ent.set<Material>(pbr);
    }
    queueDocMap(*scene, actor, 0, doc.albedoPath, previous.albedoPath);
    queueDocMap(*scene, actor, 1, doc.normalPath, previous.normalPath);
    queueDocMap(*scene, actor, 2, doc.ormPath, previous.ormPath);
    queueDocMap(*scene, actor, 3, doc.emissivePath, previous.emissivePath);
    if (doc.ormPath[0] == '\0') {
        queueDocMap(*scene, actor, 4, doc.roughPath, previous.roughPath);
        queueDocMap(*scene, actor, 5, doc.metalPath, previous.metalPath);
        queueDocMap(*scene, actor, 6, doc.aoPath, previous.aoPath);
        queueDocMap(*scene, actor, 7, doc.heightPath, previous.heightPath);
    } else {
        queueDocMap(*scene, actor, 4, "", previous.roughPath[0] != '\0' ? previous.roughPath : "x");
        queueDocMap(*scene, actor, 5, "", previous.metalPath[0] != '\0' ? previous.metalPath : "x");
        queueDocMap(*scene, actor, 6, "", previous.aoPath[0] != '\0' ? previous.aoPath : "x");
        queueDocMap(*scene, actor, 7, "", previous.heightPath[0] != '\0' ? previous.heightPath : "x");
    }
    queueDocMap(*scene, actor, 8, doc.alphaPath, previous.alphaPath);
    scene->editorDirty = static_cast<uint8_t>(scene->editorDirty | 1u);
    return true;
}

bool sceneFrameReadDoc(const SceneFrame* scene, uint8_t actor, BhmatDoc& doc) {
    doc = {};
    if (scene == nullptr || actor >= scene->actors.count) {
        return false;
    }
    flecs::entity ent = scene->actors.ent[actor];
    if (ent.is_alive() && ent.has<ActorMat>()) {
        doc = ent.get<ActorMat>().doc;
        return true;
    }
    const uint32_t id = actorMaterialSlot(*scene, actor);
    if (scene->materialSrc == nullptr || id >= scene->materialCount) {
        return false;
    }
    materialDocFromGpu(doc, scene->materialSrc[id]);
    return true;
}

uint8_t sceneFramePick(const SceneFrame* scene, float x, float y) {
    if (scene == nullptr) {
        return kSceneActorNone;
    }
    uint8_t axis = kSceneActorNone;
    return sceneActorHit(scene->actors, x, y, axis);
}

void sceneFrameMark(SceneFrame* scene, uint8_t actor) {
    if (scene == nullptr) {
        return;
    }
    scene->actors.mark = actor;
}

void takeQueuedTexture(SceneFrameImpl& s, Device& device) {
    if (s.texJobN == 0u) {
        if (s.texChannel > 7u || s.texPath[0] == '\0') {
            return;
        }
        if (s.texJobN < 8u) {
            SceneFrameImpl::TexJob& job = s.texJob[s.texJobN];
            std::snprintf(job.path, sizeof(job.path), "%s", s.texPath);
            job.actor = s.texActor;
            job.channel = s.texChannel;
            s.texJobN = static_cast<uint8_t>(s.texJobN + 1u);
        }
        s.texPath[0] = '\0';
        s.texChannel = 0xff;
    }
    while (s.texJobN > 0u && s.ready) {
        SceneFrameImpl::TexJob job = s.texJob[0];
        for (uint8_t i = 1; i < s.texJobN; ++i) {
            s.texJob[i - 1u] = s.texJob[i];
        }
        s.texJobN = static_cast<uint8_t>(s.texJobN - 1u);
        s.texPath[0] = '\0';
        s.texChannel = 0xff;
        if (job.channel > 7u || job.path[0] == '\0') {
            continue;
        }
        if (job.actor >= s.actors.count || s.textureSlots >= 768u) {
            spdlog::error("texture rejected slots {} actor {}", s.textureSlots, job.actor);
            continue;
        }
        flecs::entity ent = s.actors.ent[job.actor];
        uint32_t matId = 0xffffffffu;
        if (ent.is_alive() && ent.has<MeshBind>()) {
            matId = ent.get<MeshBind>().material;
        }
        if (matId == 0xffffffffu) {
            for (uint8_t n = 0; n < s.propCount; ++n) {
                if (s.props[n].actor == job.actor) {
                    matId = s.props[n].material;
                    break;
                }
            }
        }
        if (!ent.is_alive() || matId == 0xffffffffu || !ensureOwnedMaterials(s)) {
            spdlog::error("texture rejected, actor {} has no material", job.actor);
            continue;
        }
        const bool color = job.channel == 0u || job.channel == 3u;
        const PbrTex kind = color ? PbrTex::Color : (job.channel == 1u ? PbrTex::Normal : PbrTex::Data);
        const bool dds = endsWith(job.path, ".dds") || endsWith(job.path, ".DDS");
        GpuImage img{};
        bool loaded = dds && imageLoadDds(img, device, job.path, color);
        if (!loaded) {
            char cache[512]{};
            ddsCachePath(cache, sizeof(cache), job.path);
            if (kind == PbrTex::Normal) {
                const size_t n = std::strlen(cache);
                if (n >= 4 && n + 5 < sizeof(cache) && std::strcmp(cache + n - 4, ".dds") == 0) {
                    std::memcpy(cache + n - 4, ".bc5.dds", 9);
                }
            }
            loaded = !dds && imageLoadDds(img, device, cache, color);
            if (loaded && kind != PbrTex::Color && img.format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) {
                gpuImageDestroy(img, device);
                loaded = false;
            }
            if (!loaded) {
                ImageCpu cpu{};
                if (imageLoadFile(cpu, job.path)) {
                    if (cpu.hdr != 0 && cpu.rgb != nullptr && color) {
                        loaded = gpuImageUploadHdr(img, device, cpu.rgb, cpu.width, cpu.height);
                    } else if (cpu.rgba != nullptr) {
                        loaded = gpuImageUploadPbr(img, device, cpu.rgba, cpu.width, cpu.height, kind, cache);
                    }
                }
                imageCpuFree(cpu);
            }
        }
        if (!loaded || img.image == VK_NULL_HANDLE) {
            spdlog::error("texture load failed {}", job.path);
            continue;
        }
        const uint32_t slot = s.textureSlots;
        s.textures.push_back(img);
        s.textureSlots = slot + 1u;
        if (!heapImage(s.gpuHeap, device, s, static_cast<uint32_t>(HeapImg::Textures) + slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                s.textures[slot], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)) {
            spdlog::error("texture bind failed slot {}", slot);
            continue;
        }
        for (uint32_t flight = 0; flight < kFramesInFlight; ++flight) {
            gpuBufferFlush(s.gpuHeap.resources[flight], device, 0, s.gpuHeap.resources[flight].size);
        }
        if (s.texPublish == 0 || slot < s.texPublishBase) {
            s.texPublishBase = slot;
        }
        s.texPublish = 2;
        if (matId >= s.materialCount) {
            continue;
        }
        MaterialGpuData& mat = s.ownedMat[matId];
        if (job.channel == 0u) {
            mat.albedoTexIndex = slot;
        } else if (job.channel == 1u) {
            mat.normalTexIndex = slot;
        } else if (job.channel == 2u) {
            mat.ormTexIndex = slot;
            mat.flags |= kMatOrm;
        } else if (job.channel == 3u) {
            mat.emissiveTexIndex = slot;
        } else if (job.channel == 4u) {
            mat.pad[2] = slot;
        } else if (job.channel == 5u) {
            mat.pad[3] = slot;
        } else if (job.channel == 6u) {
            mat.aoTex = slot;
        } else if (job.channel == 7u) {
            mat.pad[0] = slot;
        } else if (job.channel == 8u) {
            mat.tail[1] = slot;
        }
        if (s.materialsBuf.mapped != nullptr) {
            static_cast<MaterialGpuData*>(s.materialsBuf.mapped)[matId] = mat;
            gpuBufferFlush(s.materialsBuf, device, 0, sizeof(MaterialGpuData) * s.materialCount);
        }
        s.editorDirty = static_cast<uint8_t>(s.editorDirty | 1u);
        spdlog::info("texture {} ch {} mat {} slot {} {}x{} mips {} fmt {}",
            job.path, job.channel, matId, slot, img.extent.width, img.extent.height, img.mips, static_cast<int>(img.format));
    }
}

void sceneFrameRecord(
    SceneFrame* scene,
    VkCommandBuffer cmd,
    Device& device,
    DescriptorHeaps& heaps,
    const Swapchain& swap,
    const FrameContext& frame) {
    if (scene == nullptr || swap.extent.width == 0 || swap.extent.height == 0) {
        return;
    }
    followHandLamp(*scene);
    if (scene->handOn && scene->instances.mapped != nullptr && scene->instanceCount > 0) {
        gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
    }
    (void)heaps;
    CpuScope recordCpu("sceneFrameRecord", &scene->cpuRecordMs);
    const bool fsrLive = (scene->passMask & kPassFsr) != 0u && scene->fsr.prepare.handle != VK_NULL_HANDLE;
    const VkExtent2D renderExtent = fsrLive ? fsrRenderExtent(scene->fsr.quality, swap.extent) : swap.extent;
    if (scene->ready && (scene->extent.width != renderExtent.width || scene->extent.height != renderExtent.height)) {
        const uint32_t other = frame.flight ^ 1u;
        VkResult otherFence = VK_SUCCESS;
        if (swap.flightFence[other] != VK_NULL_HANDLE) {
            otherFence = vkWaitForFences(device.device, 1, &swap.flightFence[other], VK_TRUE, 500'000'000ull);
        }
        if (otherFence != VK_SUCCESS) {
            spdlog::error("FSR resize held flight {} fence {} want {}x{} have {}x{}",
                other, static_cast<int>(otherFence), renderExtent.width, renderExtent.height,
                scene->extent.width, scene->extent.height);
            if (otherFence == VK_ERROR_DEVICE_LOST) {
                deviceMarkLost(device, "FSR resize", static_cast<int32_t>(otherFence));
            }
        } else {
        spdlog::info("FSR resize quality {} display {}x{} render {}x{} was {}x{}",
            scene->fsr.quality, swap.extent.width, swap.extent.height,
            renderExtent.width, renderExtent.height, scene->extent.width, scene->extent.height);
        const VkExtent2D hizExtent{renderExtent.width, renderExtent.height * 2u};
        const VkExtent2D bloomExtent{
            std::max(swap.extent.width / 2u, 1u),
            std::max(swap.extent.height / 2u, 1u) + std::max(swap.extent.height / 4u, 1u)
                + std::max(swap.extent.height / 8u, 1u) + std::max(swap.extent.height / 16u, 1u)};
        const bool resized = visPassResize(scene->vis, device, renderExtent)
            && shadePassResize(scene->shade, device, renderExtent)
            && skyPassResize(scene->sky, device, renderExtent)
            && recreateTracked(scene->hizMax[0], device, hizExtent, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->hizMax[1], device, hizExtent, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.bloom, device, bloomExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.history[0], device, renderExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.history[1], device, renderExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && restirResize(scene->restir, device, renderExtent)
            && fsrResize(scene->fsr, device, scene->gpuHeap, renderExtent, swap.extent)
            && gtaoResize(scene->gtao, device, scene->gpuHeap, renderExtent)
            && sharcResize(scene->sharc, device, scene->gpuHeap, renderExtent)
            && bindHeap(*scene, device, scene->gpuHeap);
        if (resized) {
            scene->extent = renderExtent;
            scene->tone.taaReady = 0;
        } else {
            spdlog::error("FSR resize failed quality {} render {}x{}", scene->fsr.quality, renderExtent.width, renderExtent.height);
            scene->ready = false;
        }
        }
    }
    const bool propMoved = syncPropInstance(scene);
    syncPropMaterial(*scene);
    if (propMoved && scene->instances.mapped != nullptr && scene->instanceCount > 0) {
        gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
    }
    if ((scene->editorDirty & 2u) != 0u) {
        if (scene->verts.mapped != nullptr && scene->vertCount > 0) {
            gpuBufferFlush(scene->verts, device, 0, sizeof(GpuVertex) * scene->vertCount);
        }
        if (scene->indices.mapped != nullptr && scene->indexCount > 0) {
            gpuBufferFlush(scene->indices, device, 0, sizeof(uint32_t) * scene->indexCount);
        }
        if (scene->instances.mapped != nullptr && scene->instanceCount > 0) {
            gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
        }
        if (scene->meshlets.mapped != nullptr && scene->instanceCount > 0) {
            gpuBufferFlush(scene->meshlets, device, 0, sizeof(GpuMeshlet) * scene->instanceCount);
        }
    }
    if ((scene->editorDirty & 1u) != 0u && scene->materialsBuf.mapped != nullptr && scene->materialCount > 0) {
        gpuBufferFlush(scene->materialsBuf, device, 0, sizeof(MaterialGpuData) * scene->materialCount);
    }
    scene->editorDirty = 0;
    scene->radiance.pause = 0;
    if (!scene->ready) {
        sceneFrameReleaseGpu(scene, device);
        const uint32_t vertAlloc = scene->vertCap > scene->vertCount ? scene->vertCap : scene->vertCount;
        const uint32_t indexAlloc = scene->indexCap > scene->indexCount ? scene->indexCap : scene->indexCount;
        const uint32_t instAlloc = scene->instanceCap > scene->instanceCount ? scene->instanceCap : scene->instanceCount;
        const uint32_t matAlloc = scene->materialCap > scene->materialCount ? scene->materialCap : std::max(scene->materialCount, 1u);
        const VkDeviceSize vertBytes = sizeof(GpuVertex) * vertAlloc;
        const VkDeviceSize indexBytes = sizeof(uint32_t) * indexAlloc;
        const VkDeviceSize instanceBytes = sizeof(GpuInstance) * instAlloc;
        const VkDeviceSize meshletBytes = sizeof(GpuMeshlet) * instAlloc;
        const VkDeviceSize visibleBytes = sizeof(uint32_t) * alignUp(scene->visibleCap);
        const VkDeviceSize listBytes = alignUp(visibleBytes);
        const bool buffers =
            createDefaultGpuBuffer(scene->frameBuf, device, sizeof(FrameView) * 2)
            && createDefaultGpuBuffer(scene->sunBuf, device, sizeof(FrameSun) * 2)
            && createDefaultGpuBuffer(scene->postBuf, device, sizeof(FramePost) * 2 + sizeof(GradeBlock) * 2)
            && createDefaultGpuBuffer(scene->atmBuf, device, sizeof(AtmosphereParams) * 2)
            && createDefaultGpuBuffer(scene->verts, device, vertBytes)
            && createDefaultGpuBuffer(scene->indices, device, indexBytes)
            && createDefaultGpuBuffer(scene->instances, device, instanceBytes)
            && createDefaultGpuBuffer(scene->meshlets, device, meshletBytes)
            && createDefaultGpuBuffer(scene->visible, device, listBytes * 4)
            && createDefaultGpuBuffer(scene->candidates, device, listBytes * 4)
            && createDefaultGpuBuffer(scene->indirect, device, 512, true, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            && createDefaultGpuBuffer(scene->shadowIndirect, device, 128, true, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            && createDefaultGpuBuffer(scene->shadowVisible, device, listBytes * 6)
            && createDefaultGpuBuffer(scene->pointVisible, device, listBytes * 3)
            && createDefaultGpuBuffer(scene->cubeVisible, device, listBytes * 6)
            && createDefaultGpuBuffer(scene->lightBuf, device, sizeof(LightGpuData) * kLightCap * 2)
            && createDefaultGpuBuffer(scene->cellBuf, device, sizeof(uint32_t) * 4 * 16 * 9 * 24)
            && createDefaultGpuBuffer(scene->lightIndex, device, sizeof(uint32_t) * 16 * 9 * 24 * 72)
            && createDefaultGpuBuffer(scene->decalBuf, device, sizeof(DecalGpu) * kDecalCap)
            && createDefaultGpuBuffer(scene->probeBuf, device, sizeof(ProbeGpu) * kProbeCount)
            && createDefaultGpuBuffer(scene->sectorBuf, device, sizeof(uint32_t) * std::max(instAlloc, 1u))
            && createDefaultGpuBuffer(scene->shadowPageBuf, device, sizeof(uint32_t) * 1024)
            && createDefaultGpuBuffer(scene->texPageBuf, device, sizeof(uint32_t) * 64)
            && createDefaultGpuBuffer(scene->texFeedbackBuf, device, sizeof(uint32_t) * 64)
            && createDefaultGpuBuffer(scene->materialsBuf, device, sizeof(MaterialGpuData) * matAlloc);
        const VkExtent2D pointExtent{512, 1536};
        const VkExtent2D cubeExtent{3072, 512};
        const VkExtent2D hizExtent{swap.extent.width, swap.extent.height * 2};
        const VkExtent2D shadowExtent{4096, 12288};
        const bool shadowImages =
            gpuImageCreate(scene->shadowColor[0], device, shadowExtent, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && gpuImageCreate(scene->shadowColor[1], device, shadowExtent, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateImage(scene->shadowDepth[0].image, device, shadowExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
            && recreateImage(scene->shadowDepth[1].image, device, shadowExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
            && recreateImage(scene->pointDepth.image, device, pointExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
            && recreateTracked(scene->cubeColor, device, cubeExtent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->cubeDepth, device, cubeExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
            && recreateImage(scene->hizMax[0].image, device, hizExtent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateImage(scene->hizMax[1].image, device, hizExtent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!descriptorHeapsCreate(scene->gpuHeap, device, HeapLayout{
                .buffers = 96,
                .images = static_cast<uint32_t>(HeapImg::Count),
                .samplers = 3,
            })) {
            return;
        }
        const bool passes = buffers && shadowImages
            && visPassCreate(scene->vis, device, scene->gpuHeap, swap.extent)
            && shadePassCreate(scene->shade, device, scene->gpuHeap, swap.extent)
            && skyPassCreate(scene->sky, device, scene->gpuHeap, swap.extent)
            && radianceCreate(scene->radiance, device, scene->gpuHeap)
            && sharcCreate(scene->sharc, device, scene->gpuHeap, swap.extent)
            && restirCreate(scene->restir, device, scene->gpuHeap, swap.extent)
            && fsrCreate(scene->fsr, device, scene->gpuHeap, swap.extent)
            && gtaoCreate(scene->gtao, device, scene->gpuHeap, swap.extent)
            && voxelCreate(scene->voxel, device, scene->gpuHeap)
            && hizPassCreate(scene->hiz, device, scene->gpuHeap)
            && occlPassCreate(scene->occl, device, scene->gpuHeap)
            && clusterPassCreate(scene->clusters, device, scene->gpuHeap)
            && shadowCullCreate(scene->shadowCull, device, scene->gpuHeap)
            && cubePassCreate(scene->cube, device, scene->gpuHeap)
            && tonemapPassCreate(scene->tone, device, scene->gpuHeap);
        (void)gizmoPassCreate(scene->gizmo, device, scene->gpuHeap);
        (void)particlePassCreate(scene->particles, device, scene->gpuHeap);
        (void)fogCreate(scene->fog, device, scene->gpuHeap);
        (void)rainCreate(scene->rain, device, scene->gpuHeap);
        const bool targets = passes
            && recreateTracked(scene->tone.bloom, device, VkExtent2D{
                std::max(swap.extent.width / 2u, 1u),
                std::max(swap.extent.height / 2u, 1u) + std::max(swap.extent.height / 4u, 1u)
                    + std::max(swap.extent.height / 8u, 1u) + std::max(swap.extent.height / 16u, 1u)},
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.history[0], device, swap.extent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            && recreateTracked(scene->tone.history[1], device, swap.extent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!targets || !loadTextures(*scene, device) || !bindHeap(*scene, device, scene->gpuHeap)) {
            return;
        }
        uploadStatic(*scene);
        if (scene->probeBuf.mapped != nullptr) {
            auto* probes = static_cast<ProbeGpu*>(scene->probeBuf.mapped);
            for (uint32_t i = 0; i < kProbeCount; ++i) {
                probeSetGray(probes[i], 0.16f);
            }
        }
        if (scene->sectorBuf.mapped != nullptr) {
            std::memset(scene->sectorBuf.mapped, 0, scene->sectorBuf.size);
        }
        if (scene->texPageBuf.mapped != nullptr) {
            auto* pages = static_cast<uint32_t*>(scene->texPageBuf.mapped);
            for (uint32_t i = 0; i < 64u; ++i) {
                pages[i] = 1u;
            }
        }
        if (scene->texFeedbackBuf.mapped != nullptr) {
            std::memset(scene->texFeedbackBuf.mapped, 0, scene->texFeedbackBuf.size);
        }
        if (scene->shadowPageBuf.mapped != nullptr) {
            std::memset(scene->shadowPageBuf.mapped, 0, scene->shadowPageBuf.size);
        }
        if (scene->decalBuf.mapped != nullptr) {
            std::memset(scene->decalBuf.mapped, 0, scene->decalBuf.size);
        }
        scene->extent = swap.extent;
        if (scene->candidates.mapped != nullptr) {
            std::memset(scene->candidates.mapped, 0, scene->candidates.size);
        }
        if (scene->indirect.mapped != nullptr) {
            std::memset(scene->indirect.mapped, 0, scene->indirect.size);
        }
        scene->ready = true;
    }
    if (scene->ready) {
        takeQueuedTexture(*scene, device);
    }

    CameraPose pose{};
    pose.eye[0] = scene->fly.eye[0];
    pose.eye[1] = scene->fly.eye[1];
    pose.eye[2] = scene->fly.eye[2];
    const float stepX = scene->fly.eye[0] - scene->camPrevEye[0];
    const float stepY = scene->fly.eye[1] - scene->camPrevEye[1];
    const float stepZ = scene->fly.eye[2] - scene->camPrevEye[2];
    const float step = std::sqrt(stepX * stepX + stepY * stepY + stepZ * stepZ);
    const float stepCap = step > 0.04f ? 0.04f : step;
    if (stepCap > 0.001f) {
        scene->camBob += stepCap * 4.0f;
        if (scene->camBob > 6.2831853f) {
            scene->camBob -= 6.2831853f;
        }
    }
    scene->camPrevEye[0] = scene->fly.eye[0];
    scene->camPrevEye[1] = scene->fly.eye[1];
    scene->camPrevEye[2] = scene->fly.eye[2];
    const float bob = std::sin(scene->camBob) * 0.01f;
    pose.eye[1] += bob;
    flyCamTarget(scene->fly, pose.target);
    pose.target[1] += bob + scene->camShock * 0.18f;
    pose.aspect = static_cast<float>(scene->extent.width) / static_cast<float>(scene->extent.height);
    pose.fovYRad = scene->fovYRad;
    pose.zn = scene->nearPlane > 1.0e-4f ? scene->nearPlane : 0.1f;
    pose.zf = scene->farPlane;
    if (scene->camOrtho != 0) {
        const float dx = pose.target[0] - pose.eye[0];
        const float dy = pose.target[1] - pose.eye[1];
        const float dz = pose.target[2] - pose.eye[2];
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 0.5f) {
            dist = 0.5f;
        }
        const float zoom = scene->camZoom > 0.02f ? scene->camZoom : 0.02f;
        pose.orthoHalfY = dist * std::tan(pose.fovYRad * 0.5f) * zoom;
    }
    const bool taaLive = (static_cast<uint32_t>(scene->hud.gradeFlags) & 8u) != 0u && !fsrLive;
    if (fsrLive || taaLive) {
        fsrJitter(scene->fsr, pose.jitterX, pose.jitterY, scene->extent, swap.extent);
    }
    if (!taaLive) {
        scene->tone.taaReady = 0;
    }
    CameraPose bare = pose;
    bare.jitterX = 0.0f;
    bare.jitterY = 0.0f;
    const CameraCull unjit = cameraCullBuild(bare, nullptr);
    const CameraCull cam = cameraCullBuild(pose, nullptr);
    float unjitVp[16]{};
    float jitInv[16]{};
    std::memcpy(unjitVp, unjit.viewProj.m, sizeof(unjitVp));
    std::memcpy(jitInv, cam.invViewProj.m, sizeof(jitInv));
    sceneActorSetCamera(scene->actors, cam, static_cast<float>(swap.extent.width), static_cast<float>(swap.extent.height));

    const uint32_t flight = frame.flight & 1u;
    const uint32_t strideU = static_cast<uint32_t>(alignUp(scene->visibleCap));
    scene->cube.listStride = strideU;
    const bool occl = scene->occl.compact.handle != VK_NULL_HANDLE && scene->indirect.mapped != nullptr;
    uint32_t nInst = 0;
    if (!occl) {
        nInst = cullLod(*scene, cam);
        scene->visibleCount = nInst;
        if (nInst > 0 && scene->visible.mapped != nullptr) {
            std::memcpy(scene->visible.mapped, scene->visibleCpu.data(), sizeof(uint32_t) * nInst);
        }
    }

    FrameView gpu{};
    FrameSun lit{};
    FramePost knob{};
    const std::span<const float> fog = sceneBlockSpan(scene->block, kScFog, kScFogN);
    float publishedFog = fog[0];
    std::memcpy(gpu.viewProj, cam.viewProj.m, sizeof(cam.viewProj.m));
    std::memcpy(gpu.invViewProj, cam.invViewProj.m, sizeof(cam.invViewProj.m));
    if (const Weather* climate = scene->weatherEnt.try_get<Weather>()) {
        weatherPublish(*climate, scene->atmosphere, scene->rainAmount, scene->rainWet, publishedFog);
    }
    scene->atmosphere.time = scene->skyTime;
    scene->atmosphere.parity = (scene->frameCursor & 1u) != 0u ? 1.0f : 0.0f;
    if (!scene->sunEnabled) {
        scene->atmosphere.sunIntensity = 0.0f;
    }
    atmosphereApply(lit, scene->atmosphere);
    lit.sunDirIntensity[3] *= scene->hud.sunGain;
    if (!scene->sunEnabled) {
        lit.sunDirIntensity[3] = 0.0f;
    }
    lit.pad[3] = scene->sunIndirect;
    float kelvinR = 1.0f;
    float kelvinG = 1.0f;
    float kelvinB = 1.0f;
    kelvinRgb(scene->sunKelvin, kelvinR, kelvinG, kelvinB);
    lit.sunColor[0] = scene->hud.sunR * kelvinR;
    lit.sunColor[1] = scene->hud.sunG * kelvinG;
    lit.sunColor[2] = scene->hud.sunB * kelvinB;
    gpu.cameraPos[0] = cam.pos[0];
    gpu.cameraPos[1] = cam.pos[1];
    gpu.cameraPos[2] = cam.pos[2];
    gpu.cameraPos[3] = pose.zn;
    gpu.invExtent[0] = 1.0f / static_cast<float>(scene->extent.width);
    gpu.invExtent[1] = 1.0f / static_cast<float>(scene->extent.height);
    gpu.padInv[0] = static_cast<float>(strideU);
    gpu.viewPad[0] = static_cast<float>(scene->instanceCount);
    gpu.viewPad[1] = static_cast<float>(flight * 2u * strideU);
    gpu.viewPad[2] = scene->skyTime;
    gpu.viewPad[3] = scene->rainAmount;
    const uint64_t roofTlas = scene->radiance.geometry ? static_cast<uint64_t>(scene->radiance.tlasAddress) : 0ull;
    std::memcpy(&gpu.viewTail[0], &roofTlas, sizeof(roofTlas));
    gpu.viewTail[2] = scene->rainWet;
    gpu.viewTail[3] = scene->rainRipple;
    gpu.padInv[1] = 0.0f;
    gpu.extent[0] = scene->extent.width;
    gpu.extent[1] = scene->extent.height;
    gpu.padExt[0] = swap.extent.width;
    gpu.padExt[1] = swap.extent.height;
    scene->shadowSlot = 0;
    const bool linearTarget = swap.format == VK_FORMAT_B8G8R8A8_UNORM
        || swap.format == VK_FORMAT_R8G8B8A8_UNORM
        || swap.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
        || swap.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    const float sliceNear[3] = {0.1f, 12.0f, 48.0f};
    const float sliceFar[3] = {16.0f, 64.0f, 240.0f};
    const float casterPad[3] = {8.0f, 16.0f, 32.0f};
    const float maxHalf[3] = {8.0f, 24.0f, 64.0f};
    const float snapCells[3] = {8.0f, 4.0f, 8.0f};
    float sunCx = scene->fly.eye[0];
    float sunCy = scene->fly.eye[1];
    float sunCz = scene->fly.eye[2];
    float splits[3] = {36.0f, 180.0f, 2500.0f};
    lit.sunSplit[3] = static_cast<float>(flight * 16u);
    gpu.debugMode = scene->debugMode;
    gpu.frameParity = scene->frameCursor & 1u;
    gpu.disableSSR = 1u;
    gpu.disableSSRC = 1u;
    gpu.disableHiZ = scene->showHiz ? 0u : 1u;
    gpu.disableConeCull = scene->showCone ? 0u : 1u;
    gpu.disableLod = scene->showLod ? 0u : 1u;
    gpu.disableFrustum = scene->showFrustum ? 0u : 1u;
    gpu.disableSubpixel = scene->showSubpixel ? 0u : 1u;
    gpu.disableShadow = scene->showShadow ? 0u : 1u;
    const float sunIndirect = scene->sunIndirect;
    std::memcpy(&gpu.debugPad[1], &sunIndirect, sizeof(sunIndirect));
    gpu.disableBackface = scene->showBackface ? 0u : 1u;
    gpu.disableScreen = scene->showScreen ? 0u : 1u;
    gpu.disableBehind = scene->showBehind ? 0u : 1u;
    if (scene->freeze) {
        if (!scene->freezeHeld) {
            std::memcpy(scene->freezeVp, cam.viewProj.m, sizeof(scene->freezeVp));
            scene->freezeHeld = true;
        }
    } else {
        scene->freezeHeld = false;
    }
    std::memcpy(gpu.freezeVp, scene->freezeVp, sizeof(gpu.freezeVp));
    knob.flags.forceLod = scene->forceLod;
    knob.flags.passMask = scene->passMask;
    knob.flags.lightProfile = scene->lightProfile;
    if (scene->freeze) {
        knob.flags.passMask |= 64u;
    } else {
        knob.flags.passMask &= ~64u;
    }
    const std::span<const float> gtao = sceneBlockSpan(scene->block, kScGtao, kScGtaoN);
    knob.gtao.radius = gtao[0];
    knob.gtao.steps = gtao[1];
    knob.gtao.slices = gtao[2];
    knob.gtao.power = gtao[3];
    knob.gtao.falloff = gtao[4];
    knob.gtao.intensity = scene->hud.gtao;
    knob.gtao.pad[0] = gtao[5];
    const std::span<const float> ssr = sceneBlockSpan(scene->block, kScSsr, kScSsrN);
    knob.ssr.cutoff = ssr[0];
    knob.ssr.steps = ssr[1];
    knob.ssr.stride = ssr[2];
    knob.ssr.thick = ssr[3];
    knob.ssr.pad[0] = ssr[5];
    knob.ssr.pad[1] = ssr[4];
    knob.ssr.pad[2] = ssr[6];
    knob.ssr.maxDist = ssr[7];
    knob.ssr.bias = ssr[8];
    knob.shade.sscsReach = scene->sscsReach;
    knob.shade.sscsThick = scene->sscsThick;
    knob.shade.sscsSteps = scene->sscsSteps;
    knob.shade.sscsPx = scene->sscsPx;
    knob.shade.fogDensity = publishedFog;
    knob.shade.fogHeight = fog[1];
    knob.shade.fogScatter = fog[2];
    knob.shade.sscsAmt = scene->hud.contact;
    knob.shade.specOcc = scene->hud.specOcc;
    knob.shade.toksvig = scene->hud.toksvig;
    knob.shade.pom = scene->hud.pom;
    knob.shade.ggx = scene->hud.ggx;
    knob.shade.gtaoAmt = scene->hud.gtao;
    knob.shade.lamp = scene->hud.lamp;
    knob.shade.pad0 = scene->rcIntensity;
    knob.shade.pad1 = scene->giGain;
    knob.ssrc.strength = scene->hud.ssrc;
    knob.ssrc.base = scene->ssrcBase;
    knob.ssrc.thick = scene->ssrcThick;
    knob.ssrc.cap = scene->ssrcCap;
    knob.ssrc.steps = scene->ssrcSteps;
    knob.rc.rays = scene->rcRays;
    knob.rc.spacing = scene->rcSpacing;
    knob.rc.intensity = scene->rcIntensity;
    knob.rc.maxDist = scene->rcMax;
    knob.rc.cap = 8.0f;
    knob.rc.sunScale = scene->rcSun;
    knob.rc.bounce = scene->rcBounce;
    knob.rc.fogFalloff = fog[3];
    knob.rc.fogR = fog[4];
    knob.rc.fogG = fog[5];
    knob.rc.fogB = fog[6];
    knob.rc.fogLum = fog[7];
    knob.rc.fogStart = fog[8];
    knob.rc.fogOpacity = fog[9];
    knob.rc.fogInStart = fog[10];
    knob.rc.fogInSize = fog[2];
    knob.bloom.threshold = scene->bloomThreshold;
    knob.bloom.tonemapper = scene->tonemapper;
    knob.bloom.grain = scene->hud.grain;
    knob.bloom.bloomMix = scene->hud.bloom;
    knob.bloom.vignette = scene->hud.vignette;
    knob.bloom.ca = scene->hud.ca;
    knob.bloom.pad[0] = scene->hud.brightness;
    knob.bloom.pad[1] = linearTarget ? 1.0f : 0.0f;
    knob.bloom.pad[2] = -scene->fsr.jitterX;
    knob.bloom.pad[3] = scene->fsr.jitterY;
    knob.bloom.pad[4] = scene->tone.taaReady != 0u ? 0.12f : 1.0f;
    knob.sliders.exposure = scene->hud.exposure;
    knob.sliders.temperature = scene->hud.temperature;
    knob.sliders.tint = scene->hud.tint;
    knob.sliders.contrast = scene->hud.contrast;
    knob.sliders.saturation = scene->hud.saturation;
    knob.sliders.night = scene->hud.night;
    knob.sliders.shadows = scene->hud.shadows;
    knob.sliders.midtones = scene->hud.midtones;
    knob.sliders.highlights = scene->hud.highlights;
    knob.sliders.slope = scene->hud.slope;
    knob.sliders.cdlOffset = scene->hud.cdlOffset;
    knob.sliders.power = scene->hud.power;
    knob.sliders.dofFocus = scene->hud.dofFocus;
    knob.sliders.dofAmount = scene->hud.dofAmount;
    knob.sliders.flare = scene->hud.flare;
    knob.sliders.flags = scene->hud.gradeFlags;
    gpu.debugPad[0] = scene->decalCount;
    if (scene->sunEnabled && !scene->sunCast) {
        gpu.debugPad[0] |= 0x80000000u;
    }
    gpu.debugPad[1] = kProbeGrid;
    gpu.debugPad[2] = sectorVisibleMask(scene->sectors, scene->sectorCount, scene->portals, scene->portalCount, &cam.planes[0][0]);
        if (scene->shadowPageBuf.mapped != nullptr) {
        auto* pages = static_cast<uint32_t*>(scene->shadowPageBuf.mapped);
        std::memset(pages, 0, sizeof(uint32_t) * 768u);
        const int cellX = static_cast<int>(std::floor(cam.pos[0] / 8.0f));
        const int cellZ = static_cast<int>(std::floor(cam.pos[2] / 8.0f));
        for (uint32_t c = 0; c < 3; ++c) {
            const int x0 = scene->pageCell[c][0];
            const int z0 = scene->pageCell[c][1];
            const uint32_t crossed = scene->pageReady ? shadowPagesCrossed(x0, z0, cellX, cellZ) : 256u;
            if (crossed > 0u) {
                const int xLo = scene->pageReady ? (x0 < cellX ? x0 : cellX) : cellX;
                const int xHi = scene->pageReady ? (x0 > cellX ? x0 : cellX) : cellX + 15;
                const int zLo = scene->pageReady ? (z0 < cellZ ? z0 : cellZ) : cellZ;
                const int zHi = scene->pageReady ? (z0 > cellZ ? z0 : cellZ) : cellZ + 15;
                for (int z = zLo; z <= zHi; ++z) {
                    for (int x = xLo; x <= xHi; ++x) {
                        const uint32_t px = static_cast<uint32_t>(x) & 15u;
                        const uint32_t pz = static_cast<uint32_t>(z) & 15u;
                        pages[c * 256u + pz * 16u + px] = 1u;
                    }
                }
            }
            scene->pageCell[c][0] = cellX;
            scene->pageCell[c][1] = cellZ;
        }
        scene->pageReady = true;
        gpuBufferFlush(scene->shadowPageBuf, device, 0, sizeof(uint32_t) * 768u);
    }
    if (scene->texPageBuf.mapped != nullptr && scene->texFeedbackBuf.mapped != nullptr) {
        auto* pages = static_cast<uint32_t*>(scene->texPageBuf.mapped);
        auto* feedback = static_cast<uint32_t*>(scene->texFeedbackBuf.mapped);
        if ((scene->passMask & 32768u) != 0u) {
            std::memset(pages, 0, sizeof(uint32_t) * 64u);
            pages[0] = 1u;
            const uint32_t n = feedback[0] < 63u ? feedback[0] : 63u;
            for (uint32_t i = 0; i < n; ++i) {
                pages[feedback[1u + i] & 63u] = 1u;
            }
        } else {
            for (uint32_t i = 0; i < 64u; ++i) {
                pages[i] = 1u;
            }
        }
        feedback[0] = 0u;
    }
    const VkDeviceSize shadowStride = alignUp(sizeof(uint32_t) * scene->visibleCap);
    for (uint32_t c = 0; c < 3; ++c) {
        const SunShadow sun = sunShadowSlice(
            cam, -lit.sunDirIntensity[0], -lit.sunDirIntensity[1], -lit.sunDirIntensity[2],
            sliceNear[c], sliceFar[c], casterPad[c], snapCells[c], maxHalf[c]);
        std::memcpy(lit.sunCascade[c], sun.viewProj.m, sizeof(sun.viewProj.m));
        lit.sunSplit[c] = sun.radius;
        splits[c] = sun.radius;
        if (c == 0) {
            sunCx = cam.pos[0] + cam.forward[0] * 16.0f;
            sunCy = cam.pos[1] + cam.forward[1] * 16.0f;
            sunCz = cam.pos[2] + cam.forward[2] * 16.0f;
        }
        scene->shadowCount[c] = 0;
    }
    int32_t nearId[3] = {-1, -1, -1};
    float nearD[3] = {1.0e30f, 1.0e30f, 1.0e30f};
    for (uint32_t i = 0; i < scene->importedCount; ++i) {
        LightGpuData& lamp = scene->imported[i];
        if (lamp.type != kLightPoint && lamp.type != kLightSpot) {
            lamp.shadowMapIndex = -1;
            continue;
        }
        const float dx = lamp.pos[0] - scene->fly.eye[0];
        const float dy = lamp.pos[1] - scene->fly.eye[1];
        const float dz = lamp.pos[2] - scene->fly.eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        lamp.shadowMapIndex = -1;
        for (int slot = 0; slot < 3; ++slot) {
            if (d2 >= nearD[slot]) {
                continue;
            }
            for (int shift = 2; shift > slot; --shift) {
                nearD[shift] = nearD[shift - 1];
                nearId[shift] = nearId[shift - 1];
            }
            nearD[slot] = d2;
            nearId[slot] = static_cast<int32_t>(i);
            break;
        }
    }
    float pointSig = 0.0f;
    uint32_t nearCount = 0;
    for (int slot = 0; slot < 3; ++slot) {
        if (nearId[slot] < 0) {
            continue;
        }
        scene->imported[static_cast<uint32_t>(nearId[slot])].shadowMapIndex = slot;
        scene->world.lights[nearCount] = scene->imported[static_cast<uint32_t>(nearId[slot])];
        pointSig += scene->world.lights[nearCount].pos[0] + scene->world.lights[nearCount].pos[1] + scene->world.lights[nearCount].pos[2];
        nearCount += 1;
    }
    if (nearCount > 0) {
        scene->world.lightCount = nearCount;
    }
    if (pointSig != scene->pointSig) {
        scene->pointReady = false;
        scene->pointSig = pointSig;
        scene->pointCursor = 0;
    }
    for (uint32_t i = 0; i < scene->world.lightCount && i < 3; ++i) {
        lit.pointPos[i][0] = scene->world.lights[i].pos[0];
        lit.pointPos[i][1] = scene->world.lights[i].pos[1];
        lit.pointPos[i][2] = scene->world.lights[i].pos[2];
        lit.pointPos[i][3] = scene->world.lights[i].radius;
        lit.pointColor[i][0] = scene->world.lights[i].color[0];
        lit.pointColor[i][1] = scene->world.lights[i].color[1];
        lit.pointColor[i][2] = scene->world.lights[i].color[2];
        float cd = scene->world.lights[i].intensity;
        if (cd > 0.0f && cd < 64.0f) {
            cd *= kPhotometric;
        }
        lit.pointColor[i][3] = cd / kPhotometric;
        const LightGpuData& lamp = scene->world.lights[i];
        const float dx = lamp.pos[0] - cam.pos[0];
        const float dy = lamp.pos[1] - cam.pos[1];
        const float dz = lamp.pos[2] - cam.pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float screenPx = lamp.radius / std::max(dist, 0.5f) * static_cast<float>(scene->extent.height);
        if (lamp.type == kLightSpot) {
            const uint32_t face = dominantFace(lamp.direction[0], lamp.direction[1], lamp.direction[2]);
            const float tag = static_cast<float>(face + 1u);
            lit.pad[i] = spotPage(screenPx) == 64u ? -tag : tag;
        } else {
            lit.pad[i] = 0.0f;
        }
        for (uint32_t face = 0; face < 6; ++face) {
            const Mat4 vp = pointShadowFace(lamp, face);
            std::memcpy(lit.pointFace[i][face], vp.m, sizeof(vp.m));
        }
        if (!scene->pointReady && i == scene->pointCursor) {
            scene->pointCount[i] = cullPoint(*scene, scene->world.lights[i].pos[0], scene->world.lights[i].pos[1], scene->world.lights[i].pos[2], scene->world.lights[i].radius, scene->visibleCpu.data());
            scene->pointCursor = i + 1u;
            if (scene->pointCount[i] > 0 && scene->pointVisible.mapped != nullptr) {
                std::memcpy(static_cast<uint8_t*>(scene->pointVisible.mapped) + shadowStride * i, scene->visibleCpu.data(), sizeof(uint32_t) * scene->pointCount[i]);
            }
        }
    }
    LightGpuData probe{};
    probe.type = kLightPoint;
    probe.radius = scene->shadowExtent > 1.0f ? scene->shadowExtent : 200.0f;
    lit.cubeMin[0] = scene->boxMin[0];
    lit.cubeMin[1] = scene->boxMin[1];
    lit.cubeMin[2] = scene->boxMin[2];
    lit.cubeMax[0] = scene->boxMax[0];
    lit.cubeMax[1] = scene->boxMax[1];
    lit.cubeMax[2] = scene->boxMax[2];
    const float cubeDx = scene->fly.eye[0] - scene->cubeEye[0];
    const float cubeDy = scene->fly.eye[1] - scene->cubeEye[1];
    const float cubeDz = scene->fly.eye[2] - scene->cubeEye[2];
    if (scene->cubeReady) {
        const bool fresh = scene->cubeEye[0] == 0.0f && scene->cubeEye[1] == 0.0f && scene->cubeEye[2] == 0.0f;
        if (fresh || cubeDx * cubeDx + cubeDy * cubeDy + cubeDz * cubeDz > 1600.0f) {
            scene->cubeEye[0] = scene->fly.eye[0];
            scene->cubeEye[1] = scene->fly.eye[1];
            scene->cubeEye[2] = scene->fly.eye[2];
            if (!fresh) {
                scene->cubeReady = false;
                scene->cubeFace = 0;
            }
        }
    }
    if (!scene->cubeReady && scene->cubeFace == 0u) {
        scene->cubeEye[0] = scene->fly.eye[0];
        scene->cubeEye[1] = scene->fly.eye[1];
        scene->cubeEye[2] = scene->fly.eye[2];
    }
    probe.pos[0] = scene->cubeEye[0];
    probe.pos[1] = scene->cubeEye[1];
    probe.pos[2] = scene->cubeEye[2];
    for (uint32_t face = 0; face < 6; ++face) {
        const Mat4 vp = pointShadowFace(probe, face);
        std::memcpy(lit.cubeFace[face], vp.m, sizeof(vp.m));
        if (!scene->cubeReady && face == scene->cubeFace) {
            const float dir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const float up[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
            const Mat4 view = pointShadowLook(
                probe.pos[0], probe.pos[1], probe.pos[2],
                dir[face][0], dir[face][1], dir[face][2],
                up[face][0], up[face][1], up[face][2]);
            scene->cubeCount[face] = cullFace(*scene, view, scene->visibleCpu.data());
            if (scene->cubeCount[face] > 0 && scene->cubeVisible.mapped != nullptr) {
                std::memcpy(static_cast<uint8_t*>(scene->cubeVisible.mapped) + shadowStride * face, scene->visibleCpu.data(), sizeof(uint32_t) * scene->cubeCount[face]);
            }
        }
    }
    uint32_t sunKeep[3]{};
    if (scene->shadowIndirect.mapped != nullptr) {
        gpuBufferInvalidate(scene->shadowIndirect, device, 0, scene->shadowIndirect.size);
        const auto* prevSun = static_cast<const uint32_t*>(scene->shadowIndirect.mapped) + flight * 16u;
        sunKeep[0] = prevSun[0];
        sunKeep[1] = prevSun[4];
        sunKeep[2] = prevSun[8];
        if (sunKeep[0] == 0u && sunKeep[1] == 0u && sunKeep[2] == 0u) {
            scene->shadowStampValid = 0;
        }
    }
    const LightGpuData* lightSrc = scene->importedCount > 0 ? scene->imported : scene->world.lights;
    const uint32_t lightCount = uploadLights(*scene, flight);
    gpu.padInv[1] = static_cast<float>(lightCount);
    std::memcpy(static_cast<uint8_t*>(scene->frameBuf.mapped) + sizeof(FrameView) * flight, &gpu, sizeof(gpu));
    std::memcpy(static_cast<uint8_t*>(scene->sunBuf.mapped) + sizeof(FrameSun) * flight, &lit, sizeof(lit));
    std::memcpy(static_cast<uint8_t*>(scene->postBuf.mapped) + sizeof(FramePost) * flight, &knob, sizeof(knob));
    GradeBlock grade{};
    const float bleed = scene->hud.mix;
    grade.mixR[0] = 1.0f;
    grade.mixR[1] = bleed;
    grade.mixR[2] = bleed;
    grade.mixG[0] = bleed;
    grade.mixG[1] = 1.0f;
    grade.mixG[2] = bleed;
    grade.mixB[0] = bleed;
    grade.mixB[1] = bleed;
    grade.mixB[2] = 1.0f;
    grade.shadows[0] = grade.shadows[1] = grade.shadows[2] = scene->hud.shadows;
    grade.midtones[0] = grade.midtones[1] = grade.midtones[2] = scene->hud.midtones;
    grade.highlights[0] = grade.highlights[1] = grade.highlights[2] = scene->hud.highlights;
    grade.slope[0] = grade.slope[1] = grade.slope[2] = scene->hud.slope;
    grade.offset[0] = grade.offset[1] = grade.offset[2] = scene->hud.cdlOffset;
    grade.power[0] = grade.power[1] = grade.power[2] = scene->hud.power;
    grade.ranges[0] = 0.0f;
    grade.ranges[1] = 0.333f;
    grade.ranges[2] = 0.55f;
    grade.ranges[3] = 1.0f;
    grade.generic[0] = scene->hud.genContrast;
    grade.generic[1] = scene->hud.midIn;
    grade.generic[2] = scene->hud.midOut;
    grade.generic[3] = scene->hud.hdrMax;
    grade.curves[0] = 1.0f;
    grade.curves[1] = 1.0f;
    grade.curves[2] = 1.0f;
    grade.curves[3] = scene->hud.vibrance;
    std::memcpy(grade.prev0, scene->prevVp + 0, sizeof(float) * 4);
    std::memcpy(grade.prev1, scene->prevVp + 4, sizeof(float) * 4);
    std::memcpy(grade.prev2, scene->prevVp + 8, sizeof(float) * 4);
    std::memcpy(grade.prev3, scene->prevVp + 12, sizeof(float) * 4);
    std::memcpy(static_cast<uint8_t*>(scene->postBuf.mapped) + sizeof(FramePost) * 2 + sizeof(GradeBlock) * flight, &grade, sizeof(grade));
    std::memcpy(scene->prevVp, cam.viewProj.m, sizeof(scene->prevVp));
    if (scene->atmBuf.mapped != nullptr) {
        std::memcpy(static_cast<uint8_t*>(scene->atmBuf.mapped) + sizeof(AtmosphereParams) * flight, &scene->atmosphere, sizeof(AtmosphereParams));
    }
    scene->kickHeaps = &scene->gpuHeap;
    scene->kickFlight = flight;
    if (scene->shadowIndirect.mapped != nullptr) {
        auto* shadowArgs = static_cast<uint32_t*>(scene->shadowIndirect.mapped);
        shadowArgs[flight * 16u + 12u] = scene->instanceCount;
        gpuBufferFlush(scene->shadowIndirect, device, 0, scene->shadowIndirect.size);
    }
    if (scene->gpuTime.pool == VK_NULL_HANDLE) {
        (void)gpuProfilerCreate(scene->gpuTime, device);
    }
    bool taaBound = false;
    if (taaLive && scene->tone.history[0].image.image != VK_NULL_HANDLE) {
        const uint32_t curr = flight & 1u;
        const uint32_t prev = curr ^ 1u;
        taaBound = heapWriteImageFlight(heaps, device, flight, 866u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, scene->tone.history[prev].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
            && heapWriteImageFlight(heaps, device, flight, 867u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, scene->tone.history[curr].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)
            && heapWriteImageFlight(heaps, device, flight, 868u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, scene->tone.history[curr].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    heapBind(cmd, scene->gpuHeap, flight);
    if (scene->texPublish > 0) {
        const GpuBuffer& heapBuf = scene->gpuHeap.resources[flight];
        rhiBufferBarrier(cmd, heapBuf.buffer, 0, heapBuf.size,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT);
        const uint32_t from = scene->texPublishBase;
        const uint32_t to = scene->textureSlots < scene->textures.size()
            ? scene->textureSlots
            : static_cast<uint32_t>(scene->textures.size());
        for (uint32_t i = from; i < to; ++i) {
            const GpuImage& pic = scene->textures[i];
            const uint32_t mips = pic.mips == 0u ? 1u : pic.mips;
            rhiImageBarrierMips(cmd, pic.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                0, mips);
        }
        scene->texPublish = static_cast<uint8_t>(scene->texPublish - 1u);
    }
    gpuProfilerBegin(scene->gpuTime, device, cmd, flight);
    char zoneText[3584];
    (void)gpuProfilerFormat(scene->gpuTime, zoneText, sizeof(zoneText));
    static std::chrono::steady_clock::time_point gpuLog{};
    const auto gpuNow = std::chrono::steady_clock::now();
    const bool logFrame = scene->gpuTime.count > 0 && gpuNow - gpuLog >= std::chrono::seconds(2);
    if (logFrame) {
        gpuLog = gpuNow;
    }
    bufferBarrierHostToShader(cmd, scene->postBuf.buffer, scene->postBuf.size);
    bufferBarrierHostToShader(cmd, scene->frameBuf.buffer, scene->frameBuf.size);
    bufferBarrierHostToShader(cmd, scene->sunBuf.buffer, scene->sunBuf.size);
    bufferBarrierHostToShader(cmd, scene->atmBuf.buffer, scene->atmBuf.size);
    bufferBarrierHostToShader(cmd, scene->lightBuf.buffer, scene->lightBuf.size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const RenderContext ctx{cmd, device, scene->gpuHeap, scene->gpuTime, scene->extent, flight, scene->freeze, gpu, lit, knob};
    framePassClusters(ctx, scene->clusters, scene->cellBuf, scene->lightIndex);
    CullBuffers cullBufs{scene->shadowIndirect, scene->shadowVisible, scene->indirect, scene->candidates, scene->visible, scene->instanceCount, strideU, sunKeep};
    CullResults culled{};
    culled.draw = VisInstanceDraw{scene->instances.address, scene->visible.address, scene->visibleCount};
    framePassCull(ctx, *scene, cullBufs, culled);
    GpuDebugStats stats{};
    stats.visibleInstances = scene->visibleCount;
    stats.culledInstances = scene->instanceCount - scene->visibleCount;
    stats.visibleMeshlets = scene->visibleCount;
    stats.activeLights = scene->world.lightCount;
    stats.fps = scene->fps;
    stats.frameTimeMs = scene->fps > 1.0f ? 1000.0f / scene->fps : 0.0f;
    debugFormatStats(stats, scene->title, sizeof(scene->title));
    const float sunC[3] = {sunCx, sunCy, sunCz};
    SunPassData sunData{scene->shadowDepth[flight], scene->instances, scene->shadowVisible, scene->shadowIndirect, scene->shadowStamp, scene->shadowStampValid, scene->shadowStampCount, strideU, sunC, splits};
    framePassSun(ctx, *scene, scene->vis, sunData);
    framePassPoints(ctx, scene->vis, scene->cube, scene->pointDepth, scene->cubeColor, scene->cubeDepth, scene->world.lightCount,
        scene->pointCount, scene->cubeCount, scene->pointCursor, scene->pointReady, scene->cubeReady, scene->cubeFace);
    if (!scene->cubeReady) {
        scene->cubeBaked = false;
    } else if (!scene->cubeBaked) {
        scene->cubeBaked = true;
    }
    framePassVis(ctx, scene->vis, scene->occl, scene->hiz, scene->hizMax[flight], scene->indirect, scene->candidates, scene->visible,
        scene->instanceCount, culled.twoPhase, scene->showHiz, culled.draw, culled.lateDraw);
    const bool restirOn = (scene->passMask & kPassRestir) != 0u && scene->restir.di.handle != VK_NULL_HANDLE;
    const bool gtaoOn = (scene->passMask & 524288u) == 0u && scene->lightProfile != 2u && (knob.flags.passMask & 16u) != 0u && scene->hud.gtao > 0.001f;
    framePassShade(ctx, scene->shade, scene->sky, scene->gtao, scene->vis.targets.vis, scene->vis.targets.depth, gtaoOn, scene->probeBuf.buffer, scene->probeBuf.size, restirOn, pose.fovYRad);
    const VkDeviceAddress tlas = scene->radiance.geometry ? scene->radiance.tlasAddress : 0;
    const bool worldSharc = (scene->passMask & kPassSharc) != 0u;
    const bool worldRc = !worldSharc && (scene->passMask & 128u) != 0u && (scene->passMask & 262144u) != 0u;
    const bool worldVx = !worldSharc && (scene->passMask & 524288u) != 0u && (scene->passMask & 262144u) != 0u && !worldRc;
    if (!restirOn) {
        restirNote(scene->restir, scene->lightProfile, (scene->passMask & kPassRestir) != 0u ? "нет шейдера" : "выкл");
    }
    restirStatReset(cmd, scene->restir, device, flight);
    if (restirOn) {
        const void* gpuLights = lightSrc;
        if (scene->lightBuf.mapped != nullptr) {
            gpuLights = static_cast<const uint8_t*>(scene->lightBuf.mapped) + sizeof(LightGpuData) * kLightCap * flight;
        }
        framePassRestir(ctx, scene->restir, gpu, gpuLights, lightCount, scene->passMask, tlas);
    }
    if (worldSharc) {
        framePassSharc(ctx, scene->sharc, scene->shade.hdrA, scene->giGain, lightCount, tlas, scene->restir, restirOn ? 1u : 0u, scene->sharcDenoise ? 1u : 0u);
    } else if (worldVx) {
        framePassVoxel(ctx, scene->voxel, scene->shade.hdrA, true, scene->instanceCount, propMoved);
    } else {
        framePassRadiance(ctx, scene->radiance, scene->shade.hdrA, worldRc, scene->rcRays, scene->rcSpacing, scene->rcIntensity, scene->rcMax, scene->rcSun, scene->rcBounce);
    }
    framePassShadeLate(ctx, scene->shade, scene->sky, scene->fog);
    particleRecord(cmd, device, scene->particles, scene->shade.hdrA, scene->extent, flight, scene->simDt);
    rainRecord(cmd, scene->rain, scene->shade.hdrA, scene->extent, flight, scene->simDt);
    restirStatFlush(cmd, scene->restir, flight);
    if (scene->restir.lastLog[0] != '\0') {
        std::snprintf(scene->gpuText, sizeof(scene->gpuText), "%s\n%s", scene->restir.lastLog, zoneText);
    } else {
        std::snprintf(scene->gpuText, sizeof(scene->gpuText), "%s", zoneText);
    }
    {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 32 < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\ncpu record %5.2f", static_cast<double>(scene->cpuRecordMs));
        }
    }
    const bool bloomOn = (knob.flags.passMask & 32u) != 0u && scene->hud.bloom > 0.001f;
    if (fsrLive) {
        const float dt = scene->fps > 1.0f ? 1.0f / scene->fps : 1.0f / 60.0f;
        fsrRecord(cmd, scene->fsr, device, heaps, flight, scene->vis.targets.depth, scene->shade.hdrA, dt, pose.fovYRad, pose.zn, unjitVp, jitInv);
    } else {
        fsrRestoreColor(scene->fsr, device, heaps, flight, scene->shade.hdrA.image);
        scene->fsr.lastLog[0] = '\0';
    }
    framePassTonemap(ctx, scene->tone, scene->shade.hdrA, scene->shade.hdrB, swap, frame, bloomOn, fsrLive, taaLive && taaBound);
    if (scene->fsr.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->fsr.lastLog);
        }
    }
    if (scene->gtao.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->gtao.lastLog);
        }
    }
    if (worldSharc && scene->sharc.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->sharc.lastLog);
        }
    }
    if (logFrame) {
        spdlog::info("frame {:.1f} ms ({:.1f} fps)\n{}", scene->fps > 1.0f ? 1000.0f / scene->fps : 0.0, static_cast<double>(scene->fps), scene->gpuText);
        if (scene->fsr.lastLog[0] != '\0') {
            spdlog::info("{}", scene->fsr.lastLog);
        }
        if (scene->gtao.lastLog[0] != '\0') {
            spdlog::info("{}", scene->gtao.lastLog);
        }
    }
    gizmoPassRecord(cmd, device, swap, frame, scene->gizmo, scene->actors);
}

} // namespace burnhope
