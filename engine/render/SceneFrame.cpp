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
#include <dirent.h>
#include <string>

namespace burnhope {
namespace {

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
    DIR* dir = opendir(root);
    if (dir == nullptr) {
        return;
    }
    const size_t n = std::strlen(name);
    const char* only = nullptr;
    uint32_t hits = 0;
    while (const dirent* ent = readdir(dir)) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (std::strncmp(ent->d_name, name, n) != 0) {
            continue;
        }
        only = ent->d_name;
        hits += 1;
        if (hits > 1) {
            break;
        }
    }
    closedir(dir);
    if (hits == 1 && only != nullptr) {
        std::snprintf(path, cap, "%s/%s", root, only);
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

bool loadTextures(SceneFrameImpl& s, Device& device) {
    uint32_t count = s.texNameCount == 0 ? 1u : s.texNameCount;
    if (count > 768u) {
        count = 768u;
    }
    s.textures = new GpuImage[count]{};
    s.textureSlots = count;
    const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    if (!imageUploadUncompressed(s.textures[0], device, white, 1, 1)) {
        return false;
    }
    for (uint32_t i = 1; i < count; ++i) {
        char path[320];
        const char* file = s.texNames[i].file;
        const char* root = "testscene/Textures";
        const char* name = file;
        if (file[0] != '\0' && file[1] == '/') {
            name = file + 2;
            if (file[0] == 'b') {
                root = "newscenetest/pkg_a_curtains/textures";
            } else if (file[0] == 'c') {
                root = "newscenetest/pkg_b_ivy/textures";
            } else if (file[0] == 'd') {
                root = "newscenetest/pkg_c_trees/textures";
            } else if (file[0] == 'e') {
                root = "newscenetest/pkg_d_10k_candles/textures";
            } else {
                root = "newscenetest/main_sponza/textures";
            }
        }
        resolveTexturePath(path, sizeof(path), root, name);
        char cache[512];
        ddsCachePath(cache, sizeof(cache), path);
        const bool precise = nameHas(name, "normal") || nameHas(name, "nrm") || nameHas(name, "orm")
            || nameHas(name, "rough") || nameHas(name, "metal") || nameHas(name, "spec");
        bool loaded = imageLoadDds(s.textures[i], device, cache);
        if (loaded && precise && s.textures[i].format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) {
            gpuImageDestroy(s.textures[i], device);
            loaded = false;
        }
        if (!loaded) {
            loaded = imageLoadDds(s.textures[i], device, path);
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
    return cullInstances(cam, s.instSrc, s.instanceCount, s.visibleCpu, s.visibleCap, true);
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
        use = s.textureSlots > 0 ? &s.textures[0] : &img;
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
    for (uint32_t i = 0; extra && i < 3; ++i) {
        extra = heapWriteStorage(heaps, device, static_cast<uint32_t>(HeapBuf::PointVisible) + i, s.pointVisible.address + list * i, list);
    }
    for (uint32_t i = 0; extra && i < 6; ++i) {
        extra = heapWriteStorage(heaps, device, cubeListSlot(i), s.cubeVisible.address + list * i, list);
    }
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
    if (!(extra && cascades && pictures && lamps && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Linear), sampler) && heapWriteSampler(heaps, device, static_cast<uint32_t>(HeapSamp::Wrap), wrap))) {
        return false;
    }
    for (uint32_t i = 0; i < s.textureSlots; ++i) {
        const GpuImage& tex = s.textures[i].image != VK_NULL_HANDLE ? s.textures[i] : s.textures[0];
        if (!heapImage(heaps, device, s, static_cast<uint32_t>(HeapImg::Textures) + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return false;
        }
    }
    return true;
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
    scene->visibleCpu = new uint32_t[scene->visibleCap];
    const float palette[4][3] = {{0.35f, 0.34f, 0.32f}, {0.80f, 0.25f, 0.20f}, {0.20f, 0.45f, 0.85f}, {0.85f, 0.75f, 0.30f}};
    for (uint32_t i = 0; i < 4; ++i) {
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
    delete[] scene->meshletGpu;
    scene->meshletGpu = new GpuMeshlet[view.info->meshletCount];
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
    delete[] scene->visibleCpu;
    scene->visibleCap = view.info->instanceCount;
    scene->visibleCpu = new uint32_t[scene->visibleCap];
    scene->vertCount = view.info->vertexCount;
    scene->indexCount = view.info->indexCount;
    scene->instanceCount = view.info->instanceCount;
    scene->vertSrc = reinterpret_cast<const GpuVertex*>(view.vertices);
    scene->indexSrc = view.indices;
    scene->instSrc = reinterpret_cast<const GpuInstance*>(view.instances);
    scene->meshletSrc = scene->meshletGpu;
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
    scene->ready = false;
    return true;
}

void freeOwned(SceneFrame& s) {
    delete[] s.ownedVert;
    delete[] s.ownedIndex;
    delete[] s.ownedInst;
    delete[] s.ownedMesh;
    delete[] s.ownedMat;
    delete[] s.ownedTex;
    s.ownedVert = nullptr;
    s.ownedIndex = nullptr;
    s.ownedInst = nullptr;
    s.ownedMesh = nullptr;
    s.ownedMat = nullptr;
    s.ownedTex = nullptr;
}

uint32_t remapTex(uint32_t index, uint32_t texBase) {
    return index == 0u ? 0u : texBase + index - 1u;
}

void sceneFrameShift(SceneFrame* scene, float dx, float dy, float dz) {
    if (scene == nullptr || scene->instanceCount == 0 || scene->instSrc == nullptr) {
        return;
    }
    if (scene->instSrc != scene->ownedInst) {
        auto* copy = new GpuInstance[scene->instanceCount];
        std::memcpy(copy, scene->instSrc, sizeof(GpuInstance) * scene->instanceCount);
        delete[] scene->ownedInst;
        scene->ownedInst = copy;
        scene->instSrc = scene->ownedInst;
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
    auto* verts = new GpuVertex[vertCount];
    auto* indices = new uint32_t[indexCount];
    auto* instances = new GpuInstance[instCount];
    auto* meshlets = new GpuMeshlet[instCount];
    auto* materials = new MaterialGpuData[matCount == 0 ? 1u : matCount];
    auto* names = new SceneTexName[texCount == 0 ? 1u : texCount];
    std::memcpy(verts, scene->vertSrc, sizeof(GpuVertex) * vertBase);
    std::memcpy(indices, scene->indexSrc, sizeof(uint32_t) * indexBase);
    std::memcpy(instances, scene->instSrc, sizeof(GpuInstance) * instBase);
    std::memcpy(meshlets, scene->meshletSrc, sizeof(GpuMeshlet) * instBase);
    if (scene->materialSrc != nullptr && matBase > 0) {
        std::memcpy(materials, scene->materialSrc, sizeof(MaterialGpuData) * matBase);
    }
    if (scene->texNames != nullptr && scene->texNameCount > 0) {
        std::memcpy(names, scene->texNames, sizeof(SceneTexName) * scene->texNameCount);
    }
    std::memcpy(verts + vertBase, view.vertices, sizeof(GpuVertex) * view.info->vertexCount);
    std::memcpy(indices + indexBase, view.indices, sizeof(uint32_t) * view.info->indexCount);
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
    scene->ownedVert = verts;
    scene->ownedIndex = indices;
    scene->ownedInst = instances;
    scene->ownedMesh = meshlets;
    scene->ownedMat = materials;
    scene->ownedTex = names;
    scene->vertSrc = verts;
    scene->indexSrc = indices;
    scene->instSrc = instances;
    scene->meshletSrc = meshlets;
    scene->materialSrc = materials;
    scene->texNames = names;
    scene->vertCount = vertCount;
    scene->indexCount = indexCount;
    scene->instanceCount = instCount;
    scene->materialCount = matCount;
    scene->texNameCount = texCount;
    delete[] scene->visibleCpu;
    scene->visibleCap = instCount;
    scene->visibleCpu = new uint32_t[instCount];
    delete[] scene->meshletGpu;
    scene->meshletGpu = nullptr;
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
    scene->bloomThreshold = tweaks.bloomThreshold;
    scene->fogDensity = tweaks.fogDensity;
    scene->fogHeight = tweaks.fogHeight;
    scene->fogScatter = tweaks.fogScatter;
    scene->atmosphere.sunZenith = tweaks.sunZenith;
    scene->atmosphere.sunAzimuth = tweaks.sunAzimuth;
    scene->atmosphere.cloudCoverage = tweaks.cloudCoverage;
    scene->atmosphere.cloudDensity = tweaks.cloudDensity;
    scene->tonemapper = tweaks.tonemapper > 3u ? 0u : tweaks.tonemapper;
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
    hizPassDestroy(scene->hiz, device);
    cubePassDestroy(scene->cube, device);
    tonemapPassDestroy(scene->tone, device);
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
    for (uint32_t i = 0; i < scene->textureSlots; ++i) {
        gpuImageDestroy(scene->textures[i], device);
    }
    delete[] scene->textures;
    scene->textures = nullptr;
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
    delete[] scene->meshletGpu;
    delete[] scene->visibleCpu;
    delete scene;
}

bool sceneFrameTick(SceneFrame* scene, const InputFrame& input, float dt) {
    if (scene == nullptr) {
        return false;
    }
    InputState state{};
    std::memcpy(state.keyActive, input.keyDown, sizeof(state.keyActive));
    state.mouseActive = input.pointer.down;
    state.deltaX = input.pointer.dx;
    state.deltaY = input.pointer.dy;
    state.wheelDelta = input.wheel;
    flyCamUpdate(scene->fly, state, dt);
    if (dt > 1.0e-4f) {
        scene->fps = 1.0f / dt;
        scene->frameMs[scene->frameCursor % 128u] = dt * 1000.0f;
        scene->frameCursor += 1;
        scene->skyTime += dt;
    }
    return (input.pointer.down & kPointerRight) != 0;
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
    if (mode > 10u) {
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
    const uint32_t at = n > 100u ? n / 100u : 0u;
    const float one = n > 0 && copy[at] > 0.0f ? 1000.0f / copy[at] : scene->fps;
    std::snprintf(out, cap, "fps %.0f   low %.0f   1%% %.0f   mesh %u/%u", scene->fps, low, one, scene->visibleCount, scene->instanceCount);
}

void sceneFrameKick(SceneFrame* scene, Device& device) {
    if (scene == nullptr) {
        return;
    }
    RadiancePass& rad = scene->radiance;
    const bool loaded = scene->kickHeaps != nullptr && scene->vertSrc != nullptr;
    if (loaded && !rad.geometry && rad.buildTask.stage == 0) {
        radianceArm(rad, device, *scene->kickHeaps, scene->vertSrc, scene->indexSrc, scene->instSrc, scene->meshletSrc, scene->instanceCount);
    }
    radianceKick(rad, device);
    if (gpuTaskDone(rad.traceTask, device)) {
        rad.hashLive = true;
        if (!rad.traceReady) {
            rad.traceReady = true;
            rad.hashDrawn = false;
        }
    }
    const bool world = (scene->passMask & 128u) != 0u && loaded;
    if (world && rad.geometry && !scene->freeze && rad.traceReady && rad.hashDrawn) {
        rad.traceReady = false;
        rad.hashDrawn = false;
        rad.traceTask.stage = 0;
        radianceTraceArm(rad, device, *scene->kickHeaps, scene->extent, scene->kickFlight,
            scene->rcRays, scene->rcSpacing, scene->rcIntensity, scene->rcMax);
        radianceKick(rad, device);
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
    (void)heaps;
    CpuScope recordCpu("sceneFrameRecord", &scene->cpuRecordMs);
    if (!scene->ready || scene->extent.width != swap.extent.width || scene->extent.height != swap.extent.height) {
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
            && createDefaultGpuBuffer(scene->postBuf, device, sizeof(FramePost) * 2)
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
        const VkExtent2D cubeExtent{384, 64};
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
                .buffers = 64,
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
            && hizPassCreate(scene->hiz, device, scene->gpuHeap)
            && occlPassCreate(scene->occl, device, scene->gpuHeap)
            && clusterPassCreate(scene->clusters, device, scene->gpuHeap)
            && shadowCullCreate(scene->shadowCull, device, scene->gpuHeap)
            && cubePassCreate(scene->cube, device, scene->gpuHeap)
            && tonemapPassCreate(scene->tone, device, scene->gpuHeap);
        const bool targets = passes
            && recreateTracked(scene->tone.bloom, device, VkExtent2D{
                std::max(swap.extent.width / 2u, 1u),
                std::max(swap.extent.height / 2u, 1u) + std::max(swap.extent.height / 4u, 1u)
                    + std::max(swap.extent.height / 8u, 1u) + std::max(swap.extent.height / 16u, 1u)},
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);;
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
    pose.aspect = static_cast<float>(swap.extent.width) / static_cast<float>(swap.extent.height);
    pose.zn = 0.1f;
    pose.zf = scene->farPlane;
    const CameraCull cam = cameraCullBuild(pose, nullptr);

    const uint32_t flight = frame.flight & 1u;
    const uint32_t strideU = static_cast<uint32_t>(alignUp(scene->visibleCap));
    const bool occl = scene->occl.compact[0].handle != VK_NULL_HANDLE && scene->indirect.mapped != nullptr;
    uint32_t nInst = 0;
    if (!occl) {
        nInst = cullLod(*scene, cam);
        scene->visibleCount = nInst;
        if (nInst > 0 && scene->visible.mapped != nullptr) {
            std::memcpy(scene->visible.mapped, scene->visibleCpu, sizeof(uint32_t) * nInst);
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
    gpu.invExtent[0] = 1.0f / static_cast<float>(swap.extent.width);
    gpu.invExtent[1] = 1.0f / static_cast<float>(swap.extent.height);
    gpu.padInv[0] = static_cast<float>(strideU);
    gpu.viewPad[0] = static_cast<float>(scene->instanceCount);
    gpu.viewPad[1] = static_cast<float>(flight * 2u * strideU);
    gpu.padInv[1] = 0.0f;
    gpu.extent[0] = swap.extent.width;
    gpu.extent[1] = swap.extent.height;
    scene->shadowSlot = 0;
    gpu.padExt[0] = 0.0f;
    gpu.padExt[1] = 0;
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
    knob.bloom.threshold = scene->bloomThreshold;
    knob.bloom.tonemapper = scene->tonemapper;
    knob.bloom.grain = scene->hud.grain;
    knob.bloom.bloomMix = scene->hud.bloom;
    knob.bloom.vignette = scene->hud.vignette;
    knob.bloom.ca = scene->hud.ca;
    knob.bloom.pad[0] = scene->hud.brightness;
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
            scene->pointCount[i] = cullPoint(*scene, scene->world.lights[i].pos[0], scene->world.lights[i].pos[1], scene->world.lights[i].pos[2], scene->world.lights[i].radius, scene->visibleCpu);
            scene->pointCursor = i + 1u;
            if (scene->pointCount[i] > 0 && scene->pointVisible.mapped != nullptr) {
                std::memcpy(static_cast<uint8_t*>(scene->pointVisible.mapped) + shadowStride * i, scene->visibleCpu, sizeof(uint32_t) * scene->pointCount[i]);
            }
        }
    }
    LightGpuData probe{};
    probe.type = kLightPoint;
    probe.pos[0] = (scene->boxMin[0] + scene->boxMax[0]) * 0.5f;
    probe.pos[1] = (scene->boxMin[1] + scene->boxMax[1]) * 0.5f;
    probe.pos[2] = (scene->boxMin[2] + scene->boxMax[2]) * 0.5f;
    probe.radius = scene->shadowExtent;
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
        if (fresh || cubeDx * cubeDx + cubeDy * cubeDy + cubeDz * cubeDz > 4.0f) {
            scene->cubeEye[0] = scene->fly.eye[0];
            scene->cubeEye[1] = scene->fly.eye[1];
            scene->cubeEye[2] = scene->fly.eye[2];
            if (!fresh) {
                scene->cubeReady = false;
                scene->cubeFace = 0;
            }
        }
    }
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
            scene->cubeCount[face] = cullFace(*scene, view, scene->visibleCpu);
            if (scene->cubeCount[face] > 0 && scene->cubeVisible.mapped != nullptr) {
                std::memcpy(static_cast<uint8_t*>(scene->cubeVisible.mapped) + shadowStride * face, scene->visibleCpu, sizeof(uint32_t) * scene->cubeCount[face]);
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
    gpu.padInv[1] = static_cast<float>(uploadLights(*scene, flight));
    std::memcpy(static_cast<uint8_t*>(scene->frameBuf.mapped) + sizeof(FrameView) * flight, &gpu, sizeof(gpu));
    std::memcpy(static_cast<uint8_t*>(scene->sunBuf.mapped) + sizeof(FrameSun) * flight, &lit, sizeof(lit));
    std::memcpy(static_cast<uint8_t*>(scene->postBuf.mapped) + sizeof(FramePost) * flight, &knob, sizeof(knob));
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
    if (scene->gpuTime.count > 0 && gpuNow - gpuLog >= std::chrono::seconds(2)) {
        gpuLog = gpuNow;
        spdlog::info("frame {:.1f} ms ({:.1f} fps)\n{}", scene->fps > 1.0f ? 1000.0f / scene->fps : 0.0, static_cast<double>(scene->fps), scene->gpuText);
    }
    bufferBarrierHostToShader(cmd, scene->postBuf.buffer, scene->postBuf.size);
    bufferBarrierHostToShader(cmd, scene->frameBuf.buffer, scene->frameBuf.size);
    bufferBarrierHostToShader(cmd, scene->sunBuf.buffer, scene->sunBuf.size);
    bufferBarrierHostToShader(cmd, scene->atmBuf.buffer, scene->atmBuf.size);
    bufferBarrierHostToShader(cmd, scene->lightBuf.buffer, scene->lightBuf.size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const RenderContext ctx{cmd, device, scene->gpuHeap, scene->gpuTime, swap.extent, flight, scene->freeze, gpu, lit, knob};
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
    debugFormatStats(stats, scene->title, sizeof(scene->title));
    const float sunC[3] = {sunCx, sunCy, sunCz};
    SunPassData sunData{scene->shadowDepth[flight], scene->instances, scene->shadowVisible, scene->shadowIndirect, scene->shadowStamp, scene->shadowStampValid, scene->shadowStampCount, strideU, sunC, splits};
    framePassSun(ctx, *scene, scene->vis, sunData);
    framePassPoints(ctx, scene->vis, scene->cube, scene->pointDepth, scene->cubeColor, scene->cubeDepth, scene->world.lightCount,
        scene->pointCount, scene->cubeCount, scene->pointCursor, scene->pointReady, scene->cubeReady, scene->cubeFace);
    if (!scene->cubeReady) {
        scene->cubeBaked = false;
    } else if (!scene->cubeBaked) {
        probeBakeRecord(ctx.cmd, scene->cube, scene->probeBuf.buffer, scene->probeBuf.size);
        scene->cubeBaked = true;
    }
    framePassVis(ctx, scene->vis, scene->occl, scene->hiz, scene->hizMax[flight], scene->indirect, scene->candidates, scene->visible,
        scene->instanceCount, culled.twoPhase, scene->showHiz, culled.draw, culled.lateDraw);
    const bool gtaoOn = scene->lightProfile < 2u && (knob.flags.passMask & 16u) != 0u && scene->hud.gtao > 0.001f;
    framePassShade(ctx, scene->shade, scene->sky, scene->vis.targets.vis, scene->vis.targets.depth, gtaoOn, scene->probeBuf.buffer, scene->probeBuf.size);
    const bool worldRc = (scene->passMask & 128u) != 0u;
    framePassRadiance(ctx, scene->radiance, scene->shade.hdrA, worldRc);
    const bool bloomOn = (knob.flags.passMask & 32u) != 0u && scene->hud.bloom > 0.001f;
    framePassTonemap(ctx, scene->tone, scene->shade.hdrA, scene->shade.hdrB, swap, frame, bloomOn);
}

} // namespace burnhope
