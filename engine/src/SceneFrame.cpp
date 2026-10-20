#include "platform/PlatformUtil.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/SceneFrame.hpp"
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
    const float sun[3] = {scene.fly.eye[0], scene.fly.eye[1] + 2.0f, scene.fly.eye[2] - 6.0f};
    const float post[3] = {sun[0] + 3.0f, sun[1], sun[2]};
    sceneActorAdd(scene.actors, SceneActorKind::Sun, sun, "Sun");
    if (scene.actors.count > 0) {
        Xform pose = sceneActorXform(scene.actors, 0);
        pose.euler[0] = 35.0f;
        pose.euler[1] = 30.0f;
        sceneActorWriteXform(scene.actors, 0, pose);
    }
    sceneActorAdd(scene.actors, SceneActorKind::Post, post, "Post");
    scene.actors.selected = 0;
}

} // namespace
namespace {

bool nameHas(const char* text, const char* needle);

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

void linkProp(SceneFrameImpl& s, uint32_t instance, const char* name) {
    if (s.propCount >= kPropCap || instance >= s.instanceCount) {
        return;
    }
    const float pos[3] = {
        s.cpuInstances[instance].world[3],
        s.cpuInstances[instance].world[7],
        s.cpuInstances[instance].world[11],
    };
    const uint8_t actor = sceneActorAdd(s.actors, SceneActorKind::Prop, pos, name);
    if (actor == kSceneActorNone) {
        return;
    }
    SceneFrameImpl::PropLink& link = s.props[s.propCount];
    link.instance = instance;
    link.actor = actor;
    link.radius = s.cpuInstances[instance].sphere[3];
    link.pose = sceneActorXform(s.actors, actor);
    s.propCount += 1;
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
            const uint32_t tex = mats[i].albedoTexIndex;
            if (tex == 0 || tex >= s.texNameCount || s.texNames == nullptr) {
                continue;
            }
            const char* file = s.texNames[tex].file;
            if (nameHas(file, "leaf") || nameHas(file, "foliage") || nameHas(file, "grass") || nameHas(file, "hedge")
                || nameHas(file, "fern") || nameHas(file, "azalea") || nameHas(file, "linden")) {
                mats[i].flags |= 32u;
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
            if (imageLoadFile(cpu, path) && cpu.rgba != nullptr) {
                if (precise && cpu.width * cpu.height > 0) {
                    cpu.rgba[3] = 0;
                }
                loaded = gpuImageUploadRgba(s.textures[i], device, cpu.rgba, cpu.width, cpu.height, cpu.frames, cache);
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
    wrap.maxLod = VK_LOD_CLAMP_NONE;
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
    if ((s.passMask & kPassRestir) != 0u && s.materialSrc != nullptr && s.materialCount > 0u) {
        uint32_t pick[32]{};
        float score[32]{};
        uint32_t picked = 0;
        for (uint32_t i = 0; i < s.instanceCount; ++i) {
            const GpuInstance& inst = s.cpuInstances[i];
            if (inst.materialId >= s.materialCount || inst.sphere[3] < 0.02f) {
                continue;
            }
            const MaterialGpuData& mat = s.materialSrc[inst.materialId];
            const float luma = 0.2126f * mat.baseColor[0] + 0.7152f * mat.baseColor[1] + 0.0722f * mat.baseColor[2];
            const float rank = mat.emissiveIntensity * std::max(luma, 0.05f);
            if (rank <= 0.02f) {
                continue;
            }
            if (picked < 32u) {
                pick[picked] = i;
                score[picked] = rank;
                picked += 1u;
                continue;
            }
            uint32_t worst = 0;
            for (uint32_t k = 1; k < 32u; ++k) {
                if (score[k] < score[worst]) {
                    worst = k;
                }
            }
            if (rank > score[worst]) {
                pick[worst] = i;
                score[worst] = rank;
            }
        }
        for (uint32_t n = 0; n < picked && count < kLightCap; ++n) {
            const GpuInstance& inst = s.cpuInstances[pick[n]];
            const MaterialGpuData& mat = s.materialSrc[inst.materialId];
            bool near = false;
            for (uint32_t l = 0; l < count; ++l) {
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
            LightGpuData lamp{};
            lamp.pos[0] = inst.sphere[0];
            lamp.pos[1] = inst.sphere[1];
            lamp.pos[2] = inst.sphere[2];
            lamp.radius = std::max(inst.sphere[3] * 8.0f, 4.0f);
            lamp.color[0] = mat.baseColor[0];
            lamp.color[1] = mat.baseColor[1];
            lamp.color[2] = mat.baseColor[2];
            lamp.intensity = mat.emissiveIntensity * 8.0f;
            lamp.type = kLightPoint;
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

} // namespace

struct SceneFrame : SceneFrameImpl {};

SceneFrame* sceneFrameCreate() {
    SceneFrame* scene = new SceneFrame{};
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
    if (scene->instanceCount > 1) {
        linkProp(*scene, 1, "Cube");
        scene->actors.selected = 0;
    }
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
    return scene;
}

bool sceneFrameLoadBhop(SceneFrame* scene, const char* path) {
    if (scene == nullptr || path == nullptr) {
        return false;
    }
    static_assert(sizeof(SceneVertex) == sizeof(GpuVertex));
    static_assert(sizeof(SceneInstance) == sizeof(GpuInstance));
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        return false;
    }
    SceneBlobView view{};
    if (!sceneBlobView(mapped.data, mapped.size, &view) || view.info->instanceCount == 0) {
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
    scene->materialSrc = view.materials;
    scene->materialCount = view.info->materialCount;
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
    scene->fly.eye[0] = cx;
    scene->fly.eye[1] = cy + extent * 0.15f;
    scene->fly.eye[2] = maxZ + extent * 0.05f;
    scene->fly.yaw = 0.0f;
    scene->fly.pitch = -0.15f;
    scene->fly.speed = std::max(4.0f, extent * 0.02f);
    placeActors(*scene);
    scene->propCount = 0;
    scene->propPhase = 0;
    scene->propDirty = 0;
    scene->ready = false;
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
    const float oy = scene->fly.eye[1] - 1.8f;
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
    finish(ox, oy, oz, 5.0f, 0, floorBase, floorInd, 4, 2);
    emitBox(ox - 1.5f, oy + 0.6f, oz, 0.6f, 0.6f, 0.6f, 1);
    emitBox(ox + 1.4f, oy + 0.4f, oz - 1.2f, 0.4f, 0.4f, 0.4f, 2);
    emitBox(ox + 0.2f, oy + 0.8f, oz + 1.4f, 0.8f, 0.8f, 0.8f, 3);
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
    emitBox(mirrorX, mirrorY, mirrorZ, 0.7f, 0.7f, 0.7f, 4);
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
    scene->actors.selected = 0;
    scene->ready = false;
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

bool sceneFrameAppendBhop(SceneFrame* scene, const char* path) {
    if (scene == nullptr || path == nullptr || scene->vertSrc == nullptr) {
        return false;
    }
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        return false;
    }
    SceneBlobView view{};
    if (!sceneBlobView(mapped.data, mapped.size, &view) || view.info->instanceCount == 0) {
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
        std::memcpy(&dst, &view.instances[i], sizeof(GpuInstance));
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
    for (uint32_t i = 0; i < addTex; ++i) {
        names[texBase + i] = view.textures[i + 1u];
    }
    for (uint32_t i = 0; i < view.info->materialCount; ++i) {
        MaterialGpuData& dst = materials[matBase + i];
        dst = view.materials[i];
        dst.albedoTexIndex = remapTex(dst.albedoTexIndex, texBase);
        dst.normalTexIndex = remapTex(dst.normalTexIndex, texBase);
        dst.ormTexIndex = remapTex(dst.ormTexIndex, texBase);
        dst.emissiveTexIndex = remapTex(dst.emissiveTexIndex, texBase);
        dst.pad[0] = remapTex(dst.pad[0], texBase);
        dst.pad[2] = remapTex(dst.pad[2], texBase);
        dst.pad[3] = remapTex(dst.pad[3], texBase);
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
    fileUnmap(&mapped);
    scene->ready = false;
    return true;
}

void sceneFrameTweaks(SceneFrame* scene, const SceneTweaks& tweaks) {
    if (scene == nullptr) {
        return;
    }
    scene->forceLod = tweaks.forceLod;
    scene->passMask = tweaks.passMask;
    scene->lightProfile = tweaks.lightProfile;
    scene->gtaoRadius = tweaks.gtaoRadius;
    scene->gtaoSteps = tweaks.gtaoSteps;
    scene->gtaoSlices = tweaks.gtaoSlices;
    scene->gtaoPower = tweaks.gtaoPower;
    scene->gtaoFalloff = tweaks.gtaoFalloff;
    scene->gtaoDenoise = tweaks.gtaoDenoise;
    scene->ssrCutoff = tweaks.ssrCutoff;
    scene->ssrSteps = tweaks.ssrSteps;
    scene->ssrStride = tweaks.ssrStride;
    scene->ssrThick = tweaks.ssrThick;
    scene->ssrMax = tweaks.ssrMax;
    scene->ssrBias = tweaks.ssrBias;
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
    scene->restir.knobLocal = static_cast<uint32_t>(std::clamp(tweaks.restirLocal, 0.0f, 16.0f));
    scene->restir.knobSpatial = static_cast<uint32_t>(std::clamp(tweaks.restirSpatial, 0.0f, 32.0f));
    scene->restir.knobRadius = std::clamp(tweaks.restirRadius, 1.0f, 64.0f);
    scene->restir.knobHistory = static_cast<uint32_t>(std::clamp(tweaks.restirHistory, 1.0f, 64.0f));
    scene->restir.knobBoil = std::clamp(tweaks.restirBoil, 0.0f, 1.0f);
    scene->restir.knobTemporalDepth = std::clamp(tweaks.restirTemporalDepth, 0.001f, 1.0f);
    scene->restir.knobTemporalNormal = std::clamp(tweaks.restirTemporalNormal, 0.0f, 1.0f);
    scene->restir.knobTemporalBias = static_cast<uint32_t>(std::clamp(tweaks.restirTemporalBias, 0.0f, 2.0f));
    scene->restir.knobSpatialDepth = std::clamp(tweaks.restirSpatialDepth, 0.001f, 1.0f);
    scene->restir.knobSpatialNormal = std::clamp(tweaks.restirSpatialNormal, 0.0f, 1.0f);
    scene->restir.knobSpatialBias = static_cast<uint32_t>(std::clamp(tweaks.restirSpatialBias, 0.0f, 3.0f));
    scene->restir.knobSpatialBoost = static_cast<uint32_t>(std::clamp(tweaks.restirSpatialBoost, 0.0f, 32.0f));
    scene->restir.knobSunDisk = std::clamp(tweaks.restirSunDisk, 0.0f, 4.0f);
    scene->restir.knobSunRays = static_cast<uint32_t>(std::clamp(tweaks.restirSunRays, 1.0f, 8.0f));
    scene->restir.knobShadowRays = static_cast<uint32_t>(std::clamp(tweaks.restirShadowRays, 1.0f, 8.0f));
    scene->restir.knobShadowRange = std::clamp(tweaks.restirShadowRange, 0.0f, 200.0f);
    scene->restir.knobShadowBias = std::clamp(tweaks.restirShadowBias, 0.02f, 2.0f);
    scene->restir.knobPermTh = std::clamp(tweaks.restirPermTh, 0.0f, 1.0f);
    scene->restir.knobTarget = static_cast<uint32_t>(std::clamp(tweaks.restirTarget, 1.0f, 64.0f));
    scene->restir.knobTemporalOn = tweaks.restirTemporal ? 1u : 0u;
    scene->restir.knobSpatialOn = tweaks.restirSpatialOn ? 1u : 0u;
    scene->restir.knobBoilOn = tweaks.restirBoilOn ? 1u : 0u;
    scene->restir.knobInitVis = tweaks.restirInitVis ? 1u : 0u;
    scene->restir.knobPerm = tweaks.restirPerm ? 1u : 0u;
    scene->restir.knobShortcut = tweaks.restirShortcut ? 1u : 0u;
    scene->restir.knobMaterial = tweaks.restirMaterial ? 1u : 0u;
    scene->restir.knobNaive = tweaks.restirNaive ? 1u : 0u;
    scene->restir.knobSunOn = tweaks.restirSunOn ? 1u : 0u;
    scene->fsr.quality = tweaks.fsrQuality > 4u ? 0u : tweaks.fsrQuality;
    scene->fsr.sharpness = std::clamp(tweaks.fsrSharpness, 0.0f, 1.0f);
    scene->bloomThreshold = tweaks.bloomThreshold;
    scene->fogDensity = tweaks.fogDensity;
    scene->fogHeight = tweaks.fogHeight;
    scene->fogScatter = tweaks.fogScatter;
    scene->fogFalloff = tweaks.fogFalloff;
    scene->fogR = tweaks.fogR;
    scene->fogG = tweaks.fogG;
    scene->fogB = tweaks.fogB;
    scene->fogLum = tweaks.fogLum;
    scene->fogStart = tweaks.fogStart;
    scene->fogOpacity = tweaks.fogOpacity;
    scene->fogInStart = tweaks.fogInStart;
    scene->atmosphere.sunZenith = tweaks.sunZenith;
    scene->atmosphere.sunAzimuth = tweaks.sunAzimuth;
    scene->atmosphere.cloudCoverage = tweaks.cloudCoverage;
    scene->atmosphere.cloudDensity = tweaks.cloudDensity;
    scene->tonemapper = tweaks.tonemapper > 7u ? 0u : tweaks.tonemapper;
    scene->freeze = tweaks.freeze;
}

const char* sceneFrameGpuText(const SceneFrame* scene) {
    if (scene == nullptr) {
        return "";
    }
    return scene->gpuText;
}

void sceneFrameTune(SceneFrame* scene, const HudPost& post) {
    if (scene == nullptr) {
        return;
    }
    scene->hud = post;
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
    sceneActorPointer(
        scene->actors,
        input.pointer.x,
        input.pointer.y,
        (input.pointer.down & kPointerLeft) != 0 ? 1 : 0,
        (input.pointer.pressed & kPointerLeft) != 0 ? 1 : 0,
        (input.pointer.released & kPointerLeft) != 0 ? 1 : 0);
    flyCamUpdate(scene->fly, input, dt);
    if (dt > 1.0e-4f) {
        scene->skyTime += dt;
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
    scene->actors.gizmo = mode > 2 ? 0 : mode;
    scene->actors.drag = kSceneActorNone;
}

uint8_t sceneFrameActorDuplicate(SceneFrame* scene) {
    if (scene == nullptr) {
        return kSceneActorNone;
    }
    return sceneActorDuplicate(scene->actors);
}

bool sceneFrameActorDragging(const SceneFrame* scene) {
    return scene != nullptr && scene->actors.drag < 3;
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

void sceneFrameDebug(SceneFrame* scene, bool hiz, bool cone, bool lod, bool frustum, bool subpixel, bool shadow, uint32_t mode) {
    if (scene == nullptr) {
        return;
    }
    if (mode > 19u) {
        mode = 0;
    }
    if (scene->showHiz == hiz && scene->showCone == cone
        && scene->showLod == lod && scene->showFrustum == frustum && scene->showSubpixel == subpixel
        && scene->showShadow == shadow && scene->debugMode == mode) {
        return;
    }
    scene->showHiz = hiz;
    scene->showCone = cone;
    scene->showLod = lod;
    scene->showFrustum = frustum;
    scene->showSubpixel = subpixel;
    scene->showShadow = shadow;
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

bool syncPropInstance(SceneFrame* scene) {
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
    if (propMoved && scene->instances.mapped != nullptr && scene->instanceCount > 0) {
        gpuBufferFlush(scene->instances, device, 0, sizeof(GpuInstance) * scene->instanceCount);
    }
    if (propMoved) {
        scene->propDirty = 1;
    }
    if (scene->propDirty != 0 && scene->radiance.geometry && scene->propPhase != 2) {
        scene->propPhase = 1;
        scene->radiance.pause = 0;
        scene->propDirty = 0;
    } else if (scene->propPhase == 1) {
        scene->radiance.pause = 1;
        scene->propPhase = 2;
    } else if (scene->propPhase == 2) {
        radianceDrop(scene->radiance, device);
        scene->propPhase = 0;
        scene->propDirty = 0;
    } else {
        scene->radiance.pause = 0;
    }
    if (!scene->ready) {
        sceneFrameReleaseGpu(scene, device);
        const VkDeviceSize vertBytes = sizeof(GpuVertex) * scene->vertCount;
        const VkDeviceSize indexBytes = sizeof(uint32_t) * scene->indexCount;
        const VkDeviceSize instanceBytes = sizeof(GpuInstance) * scene->instanceCount;
        const VkDeviceSize meshletBytes = sizeof(GpuMeshlet) * scene->instanceCount;
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
            && createDefaultGpuBuffer(scene->sectorBuf, device, sizeof(uint32_t) * std::max(scene->instanceCount, 1u))
            && createDefaultGpuBuffer(scene->shadowPageBuf, device, sizeof(uint32_t) * 1024)
            && createDefaultGpuBuffer(scene->texPageBuf, device, sizeof(uint32_t) * 64)
            && createDefaultGpuBuffer(scene->texFeedbackBuf, device, sizeof(uint32_t) * 64)
            && createDefaultGpuBuffer(scene->materialsBuf, device, sizeof(MaterialGpuData) * std::max(scene->materialCount, 1u));
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
                .buffers = 75,
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

    CameraPose pose{};
    pose.eye[0] = scene->fly.eye[0];
    pose.eye[1] = scene->fly.eye[1];
    pose.eye[2] = scene->fly.eye[2];
    flyCamTarget(scene->fly, pose.target);
    pose.aspect = static_cast<float>(scene->extent.width) / static_cast<float>(scene->extent.height);
    pose.zn = 0.1f;
    pose.zf = scene->farPlane;
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
    std::memcpy(gpu.viewProj, cam.viewProj.m, sizeof(cam.viewProj.m));
    std::memcpy(gpu.invViewProj, cam.invViewProj.m, sizeof(cam.invViewProj.m));
    scene->atmosphere.time = scene->skyTime;
    scene->atmosphere.parity = (scene->frameCursor & 1u) != 0u ? 1.0f : 0.0f;
    atmosphereApply(lit, scene->atmosphere);
    lit.sunDirIntensity[3] *= scene->hud.sunGain;
    lit.sunColor[0] = scene->hud.sunR;
    lit.sunColor[1] = scene->hud.sunG;
    lit.sunColor[2] = scene->hud.sunB;
    gpu.cameraPos[0] = cam.pos[0];
    gpu.cameraPos[1] = cam.pos[1];
    gpu.cameraPos[2] = cam.pos[2];
    gpu.cameraPos[3] = pose.zn;
    gpu.invExtent[0] = 1.0f / static_cast<float>(scene->extent.width);
    gpu.invExtent[1] = 1.0f / static_cast<float>(scene->extent.height);
    gpu.padInv[0] = static_cast<float>(strideU);
    gpu.viewPad[0] = static_cast<float>(scene->instanceCount);
    gpu.viewPad[1] = static_cast<float>(flight * 2u * strideU);
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
    gpu.debugAlign[0] = scene->frameCursor & 1u;
    gpu.disableSSR = 1u;
    gpu.disableSSRC = 1u;
    gpu.disableHiZ = scene->showHiz ? 0u : 1u;
    gpu.disableConeCull = scene->showCone ? 0u : 1u;
    gpu.disableLod = scene->showLod ? 0u : 1u;
    gpu.disableFrustum = scene->showFrustum ? 0u : 1u;
    gpu.disableSubpixel = scene->showSubpixel ? 0u : 1u;
    gpu.disableShadow = scene->showShadow ? 0u : 1u;
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
    knob.gtao.radius = scene->gtaoRadius;
    knob.gtao.steps = scene->gtaoSteps;
    knob.gtao.slices = scene->gtaoSlices;
    knob.gtao.power = scene->gtaoPower;
    knob.gtao.falloff = scene->gtaoFalloff;
    knob.gtao.intensity = scene->hud.gtao;
    knob.gtao.pad[0] = scene->gtaoDenoise;
    knob.ssr.cutoff = scene->ssrCutoff;
    knob.ssr.steps = scene->ssrSteps;
    knob.ssr.stride = scene->ssrStride;
    knob.ssr.thick = scene->ssrThick;
    knob.ssr.maxDist = scene->ssrMax;
    knob.ssr.bias = scene->ssrBias;
    knob.shade.sscsReach = scene->sscsReach;
    knob.shade.sscsThick = scene->sscsThick;
    knob.shade.sscsSteps = scene->sscsSteps;
    knob.shade.sscsPx = scene->sscsPx;
    knob.shade.fogDensity = scene->fogDensity;
    knob.shade.fogHeight = scene->fogHeight;
    knob.shade.fogScatter = scene->fogScatter;
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
    knob.rc.fogFalloff = scene->fogFalloff;
    knob.rc.fogR = scene->fogR;
    knob.rc.fogG = scene->fogG;
    knob.rc.fogB = scene->fogB;
    knob.rc.fogLum = scene->fogLum;
    knob.rc.fogStart = scene->fogStart;
    knob.rc.fogOpacity = scene->fogOpacity;
    knob.rc.fogInStart = scene->fogInStart;
    knob.rc.fogInSize = scene->fogScatter;
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
        lit.pointColor[i][3] = 2.0f;
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
            && heapWriteImageFlight(heaps, device, flight, 868u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, scene->tone.history[curr].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    heapBind(cmd, scene->gpuHeap, flight);
    gpuProfilerBegin(scene->gpuTime, device, cmd, flight);
    (void)gpuProfilerFormat(scene->gpuTime, scene->gpuText, sizeof(scene->gpuText));
    {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 32 < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\ncpu record %5.2f", static_cast<double>(scene->cpuRecordMs));
        }
    }
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
    framePassShadeLate(ctx, scene->shade, scene->sky);
    const bool bloomOn = (knob.flags.passMask & 32u) != 0u && scene->hud.bloom > 0.001f;
    if (fsrLive) {
        const float dt = scene->fps > 1.0f ? 1.0f / scene->fps : 1.0f / 60.0f;
        fsrRecord(cmd, scene->fsr, device, heaps, flight, scene->vis.targets.depth, scene->shade.hdrA, dt, pose.fovYRad, pose.zn, unjitVp, jitInv);
    } else {
        fsrRestoreColor(scene->fsr, device, heaps, flight, scene->shade.hdrA.image);
        scene->fsr.lastLog[0] = '\0';
    }
    framePassTonemap(ctx, scene->tone, scene->shade.hdrA, scene->shade.hdrB, swap, frame, bloomOn, fsrLive, taaLive && taaBound);
    if (scene->restir.lastLog[0] != '\0') {
        const uint32_t used = static_cast<uint32_t>(std::strlen(scene->gpuText));
        if (used + 2u < sizeof(scene->gpuText)) {
            std::snprintf(scene->gpuText + used, sizeof(scene->gpuText) - used, "\n%s", scene->restir.lastLog);
        }
    }
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
    gizmoPassRecord(cmd, swap, frame, scene->gizmo, scene->actors);
}

} // namespace burnhope
