#include "render/Voxel.hpp"

#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/RenderDefs.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

namespace burnhope {

namespace {

constexpr uint32_t kVxRes = 128;
constexpr float kVxExtent[3] = {16.0f, 64.0f, 256.0f};
constexpr VkDeviceSize kVxVoxels = 128ull * 128ull * 128ull;
constexpr VkDeviceSize kVxSlice = kVxVoxels * 16ull;

int32_t vxWrap(int32_t v) {
    v %= static_cast<int32_t>(kVxRes);
    if (v < 0) {
        v += static_cast<int32_t>(kVxRes);
    }
    return v;
}

int32_t vxSnap(float cam, float voxel) {
    return static_cast<int32_t>(std::floor(static_cast<double>(cam) / static_cast<double>(voxel))) - static_cast<int32_t>(kVxRes / 2u);
}

void vxFillFull(VxCascade& out, int32_t ax, int32_t ay, int32_t az) {
    out.scrollX = 0;
    out.scrollY = 0;
    out.scrollZ = 0;
    out.full = 1;
    out.slabMinX = ax;
    out.slabMaxX = ax + static_cast<int32_t>(kVxRes);
    out.slabMinY = ay;
    out.slabMaxY = ay + static_cast<int32_t>(kVxRes);
    out.slabMinZ = az;
    out.slabMaxZ = az + static_cast<int32_t>(kVxRes);
}

void vxShift(
    int32_t anchor[3],
    int32_t scroll[3],
    uint8_t& live,
    float camX,
    float camY,
    float camZ,
    float extent,
    bool force,
    VxCascade& out) {
    const float voxel = extent / static_cast<float>(kVxRes);
    const int32_t ax = vxSnap(camX, voxel);
    const int32_t ay = vxSnap(camY, voxel);
    const int32_t az = vxSnap(camZ, voxel);
    out = {};
    out.anchorX = ax;
    out.anchorY = ay;
    out.anchorZ = az;
    out.voxel = voxel;
    out.extent = extent;
    if (live == 0 || force) {
        anchor[0] = ax;
        anchor[1] = ay;
        anchor[2] = az;
        scroll[0] = 0;
        scroll[1] = 0;
        scroll[2] = 0;
        live = 1;
        vxFillFull(out, ax, ay, az);
        return;
    }
    const int32_t dx = ax - anchor[0];
    const int32_t dy = ay - anchor[1];
    const int32_t dz = az - anchor[2];
    if (dx == 0 && dy == 0 && dz == 0) {
        out.scrollX = scroll[0];
        out.scrollY = scroll[1];
        out.scrollZ = scroll[2];
        out.full = 0;
        return;
    }
    if (std::abs(dx) >= static_cast<int32_t>(kVxRes) || std::abs(dy) >= static_cast<int32_t>(kVxRes) || std::abs(dz) >= static_cast<int32_t>(kVxRes)) {
        anchor[0] = ax;
        anchor[1] = ay;
        anchor[2] = az;
        scroll[0] = 0;
        scroll[1] = 0;
        scroll[2] = 0;
        live = 1;
        vxFillFull(out, ax, ay, az);
        return;
    }
    scroll[0] = vxWrap(scroll[0] + dx);
    scroll[1] = vxWrap(scroll[1] + dy);
    scroll[2] = vxWrap(scroll[2] + dz);
    anchor[0] = ax;
    anchor[1] = ay;
    anchor[2] = az;
    out.scrollX = scroll[0];
    out.scrollY = scroll[1];
    out.scrollZ = scroll[2];
    out.full = 0;
    const int32_t delta[3] = {dx, dy, dz};
    const int32_t next[3] = {ax, ay, az};
    int32_t smin[3] = {};
    int32_t smax[3] = {};
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        if (delta[axis] > 0) {
            smin[axis] = next[axis] + static_cast<int32_t>(kVxRes) - delta[axis];
            smax[axis] = next[axis] + static_cast<int32_t>(kVxRes);
        } else if (delta[axis] < 0) {
            smin[axis] = next[axis];
            smax[axis] = next[axis] - delta[axis];
        }
    }
    out.slabMinX = smin[0];
    out.slabMinY = smin[1];
    out.slabMinZ = smin[2];
    out.slabMaxX = smax[0];
    out.slabMaxY = smax[1];
    out.slabMaxZ = smax[2];
}

bool vxDirty(const VxCascade& c) {
    return c.full != 0u || c.slabMaxX > c.slabMinX || c.slabMaxY > c.slabMinY || c.slabMaxZ > c.slabMinZ;
}

bool vxShader(Device& d, const char* path, const PassBindings& bind, ShaderExt& out) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
        .pushBytes = sizeof(VxPush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    return shaderCreate(d, desc, out);
}

void vxBind(VkCommandBuffer cmd, const ShaderExt& shader, VxPush push) {
    cmdBindCompute(cmd, shader.handle);
    vkCmdPushConstants(cmd, VK_NULL_HANDLE, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
}

void vxToGeneral(VkCommandBuffer cmd, TrackedImage& img) {
    if (img.image.image == VK_NULL_HANDLE) {
        return;
    }
    if (img.layout == VK_IMAGE_LAYOUT_GENERAL) {
        return;
    }
    const VkImageLayout old = img.layout;
    const VkPipelineStageFlags2 srcStage = old == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : img.stage;
    const VkAccessFlags2 srcAccess = old == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_2_NONE : img.access;
    rhiImageBarrierMips(
        cmd, img.image.image, old, VK_IMAGE_LAYOUT_GENERAL, srcStage, srcAccess,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        0u, img.image.mips);
    img.layout = VK_IMAGE_LAYOUT_GENERAL;
    img.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    img.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
}

void vxMips(VkCommandBuffer cmd, TrackedImage& img) {
    VkImage image = img.image.image;
    const uint32_t mips = img.image.mips;
    const uint32_t side = img.image.extent.width;
    if (image == VK_NULL_HANDLE || mips < 2u || side < 2u) {
        return;
    }
    rhiImageBarrierMips(
        cmd, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, 0u, 1u);
    rhiImageBarrierMips(
        cmd, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1u, mips - 1u);
    for (uint32_t mip = 1; mip < mips; ++mip) {
        if (mip > 1u) {
            rhiImageBarrierMips(
                cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, mip - 1u, 1u);
        }
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1u, 0u, 1u};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0u, 1u};
        const int32_t src = static_cast<int32_t>(std::max(side >> (mip - 1u), 1u));
        const int32_t dst = static_cast<int32_t>(std::max(side >> mip, 1u));
        region.srcOffsets[1] = {src, src, src};
        region.dstOffsets[1] = {dst, dst, dst};
        vkCmdBlitImage(
            cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1u, &region, VK_FILTER_LINEAR);
    }
    const VkAccessFlags2 read = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    rhiImageBarrierMips(
        cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, read, 0u, mips - 1u);
    rhiImageBarrierMips(
        cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, read, mips - 1u, 1u);
    img.layout = VK_IMAGE_LAYOUT_GENERAL;
    img.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    img.access = read;
}

void vxAtomBarrier(VkCommandBuffer cmd, const GpuBuffer& atoms, uint32_t flight, uint32_t cascade) {
    const VkDeviceSize offset = (static_cast<VkDeviceSize>(flight) * 3ull + cascade) * kVxSlice;
    rhiBufferBarrier(
        cmd, atoms.buffer, offset, kVxSlice,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

} // namespace

bool voxelCreate(VoxelPass& p, Device& d, const DescriptorHeaps& heaps) {
    voxelDestroy(p, d);
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    bool pics = true;
    for (uint32_t flight = 0; pics && flight < 2u; ++flight) {
        for (uint32_t cascade = 0; cascade < 3u; ++cascade) {
            pics = gpuImageCreateVolume(
                p.volume[flight][cascade].image, d, kVxRes, VK_FORMAT_R16G16B16A16_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT);
            p.volume[flight][cascade].layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }
    if (!pics) {
        spdlog::error("voxel volumes failed");
        voxelDestroy(p, d);
        return false;
    }
    if (!createDefaultGpuBuffer(p.atoms, d, kVxSlice * 3ull * 2ull, false)
        || !createDefaultGpuBuffer(p.clip, d, 256ull * 2ull, true)) {
        spdlog::error("voxel buffers failed");
        voxelDestroy(p, d);
        return false;
    }
    PassBindings clear{heaps};
    clear.storageBuf(17, HeapBuf::Voxel, false);
    clear.knob(59, HeapBuf::VxClip);
    PassBindings voxel{heaps};
    voxel.ubo(0, HeapBuf::Frame);
    voxel.storageBuf(7, HeapBuf::Verts);
    voxel.storageBuf(8, HeapBuf::Indices);
    voxel.storageBuf(9, HeapBuf::Meshlets);
    voxel.storageBuf(10, HeapBuf::Instances);
    voxel.sampledImg(12, HeapImg::Textures);
    voxel.storageBuf(14, HeapBuf::Materials);
    voxel.sampledImg(15, HeapImg::Shadow);
    voxel.storageBuf(17, HeapBuf::Voxel, false);
    voxel.sampler(24, HeapSamp::Wrap);
    voxel.sun();
    voxel.knob(59, HeapBuf::VxClip);
    PassBindings resolve{heaps};
    resolve.storageBuf(17, HeapBuf::Voxel, false);
    resolve.storageImg(60, HeapImg::VoxelStore);
    resolve.knob(59, HeapBuf::VxClip);
    PassBindings trace{heaps};
    trace.ubo(0, HeapBuf::Frame);
    trace.sun();
    trace.sampledImg(1, HeapImg::Vis);
    trace.sampledImg(2, HeapImg::Depth);
    trace.storageImg(3, HeapImg::HdrAStorage);
    trace.storageImg(4, HeapImg::Surf0);
    trace.storageImg(5, HeapImg::Surf1);
    trace.sampler(23, HeapSamp::Linear);
    trace.sampler(24, HeapSamp::Wrap);
    trace.sampledImg(42, HeapImg::SkyViewSample);
    trace.knob(50, HeapBuf::Flags);
    trace.knob(55, HeapBuf::Shade);
    trace.knob(59, HeapBuf::VxClip);
    trace.sampledImg(61, HeapImg::VoxelSample);
    const bool ok = vxShader(d, BH_SHADER_DIR "/vx.clear.comp.spv", clear, p.clear)
        && vxShader(d, BH_SHADER_DIR "/vx.voxel.comp.spv", voxel, p.voxel)
        && vxShader(d, BH_SHADER_DIR "/vx.resolve.comp.spv", resolve, p.resolve)
        && vxShader(d, BH_SHADER_DIR "/vx.trace.comp.spv", trace, p.trace);
    if (!ok) {
        spdlog::error("voxel shaders failed");
        voxelDestroy(p, d);
        return false;
    }
    return true;
}

void voxelDestroy(VoxelPass& p, Device& d) {
    for (uint32_t flight = 0; flight < 2u; ++flight) {
        for (uint32_t cascade = 0; cascade < 3u; ++cascade) {
            gpuImageDestroy(p.volume[flight][cascade].image, d);
            p.volume[flight][cascade].layout = VK_IMAGE_LAYOUT_UNDEFINED;
            p.volume[flight][cascade].stage = VK_PIPELINE_STAGE_2_NONE;
            p.volume[flight][cascade].access = VK_ACCESS_2_NONE;
            p.live[flight][cascade] = 0;
        }
    }
    gpuBufferDestroy(p.atoms, d);
    gpuBufferDestroy(p.clip, d);
    shaderDestroy(d, p.clear);
    shaderDestroy(d, p.voxel);
    shaderDestroy(d, p.resolve);
    shaderDestroy(d, p.trace);
    p.sunSet = 0;
    p.bakedInstances = 0;
}

void voxelRecord(
    VkCommandBuffer cmd,
    VoxelPass& p,
    Device& d,
    VkExtent2D extent,
    uint32_t flight,
    uint32_t instances,
    bool moved,
    const FrameView& view,
    const FrameSun& sun) {
    if (p.trace.handle == VK_NULL_HANDLE || p.clip.mapped == nullptr || flight > 1u) {
        return;
    }
    flight &= 1u;
    bool force = moved || p.bakedInstances != instances;
    const float dir[4] = {sun.sunDirIntensity[0], sun.sunDirIntensity[1], sun.sunDirIntensity[2], sun.sunDirIntensity[3]};
    if (p.sunSet == 0
        || std::fabs(dir[0] - p.sun[0]) > 1.0e-3f
        || std::fabs(dir[1] - p.sun[1]) > 1.0e-3f
        || std::fabs(dir[2] - p.sun[2]) > 1.0e-3f
        || std::fabs(dir[3] - p.sun[3]) > 0.05f) {
        force = true;
        p.sun[0] = dir[0];
        p.sun[1] = dir[1];
        p.sun[2] = dir[2];
        p.sun[3] = dir[3];
        p.sunSet = 1;
    }
    p.bakedInstances = instances;
    VxCascade cas[3]{};
    for (uint32_t c = 0; c < 3u; ++c) {
        vxShift(p.anchor[flight][c], p.scroll[flight][c], p.live[flight][c], view.cameraPos[0], view.cameraPos[1], view.cameraPos[2], kVxExtent[c], force, cas[c]);
    }
    std::memcpy(static_cast<uint8_t*>(p.clip.mapped) + static_cast<size_t>(flight) * 256u, cas, sizeof(cas));
    gpuBufferFlush(p.clip, d, static_cast<VkDeviceSize>(flight) * 256ull, 256ull);
    bufferBarrierHostToShader(cmd, p.clip.buffer, p.clip.size);
    VxPush push{};
    push.flight = flight;
    push.instances = instances;
    for (uint32_t c = 0; c < 3u; ++c) {
        if (!vxDirty(cas[c]) || p.volume[flight][c].image.image == VK_NULL_HANDLE) {
            continue;
        }
        push.cascade = c;
        vxToGeneral(cmd, p.volume[flight][c]);
        vxBind(cmd, p.clear, push);
        vkCmdDispatch(cmd, (kVxRes * kVxRes * kVxRes) / 64u, 1u, 1u);
        vxAtomBarrier(cmd, p.atoms, flight, c);
        if (instances > 0u && p.voxel.handle != VK_NULL_HANDLE) {
            vxBind(cmd, p.voxel, push);
            vkCmdDispatch(cmd, (instances + 63u) / 64u, 1u, 1u);
            vxAtomBarrier(cmd, p.atoms, flight, c);
        }
        vxBind(cmd, p.resolve, push);
        vkCmdDispatch(cmd, (kVxRes * kVxRes * kVxRes) / 64u, 1u, 1u);
        vxMips(cmd, p.volume[flight][c]);
    }
    push.cascade = 0;
    vxBind(cmd, p.trace, push);
    dispatch2D(cmd, extent, 8u);
}

} // namespace burnhope
