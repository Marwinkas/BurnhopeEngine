#pragma once

#include "gpu_scene/Mat4.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

struct GpuVertex {
    float px, py, pz, pad0;
    float nx, ny, nz, pad1;
};

struct FrameGPU {
    float viewProj[16];
    float invViewProj[16];
    float sunDirIntensity[4];
    float sunColor[4];
    float cameraPos[4];
    float invExtent[2];
    float padInv[2];
    uint32_t extent[2];
    uint32_t padExt[2];
};

struct MeshletGpu {
    GpuBuffer verts;
    GpuBuffer indices;
    GpuBuffer frame;
};

[[nodiscard]] bool meshletGpuCreate(MeshletGpu& s, Device& d);
void meshletGpuDestroy(MeshletGpu& s, Device& d);
void meshletGpuWriteFrame(MeshletGpu& s, VkExtent2D extent);

} // namespace burnhope
