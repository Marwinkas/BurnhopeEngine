#include "gpu_scene/MeshletGpu.hpp"
#include "gpu_scene/Camera.hpp"

#include <cstring>

namespace burnhope {

bool meshletGpuCreate(MeshletGpu& s, Device& d) {
    meshletGpuDestroy(s, d);
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    GpuVertex verts[3]{
        {-0.85f, -0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {0.85f, -0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
    };
    uint32_t indices[3]{0, 1, 2};

    if (!gpuBufferCreate(s.verts, d, sizeof(verts),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)
        || !gpuBufferCreate(s.indices, d, sizeof(indices),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)
        || !gpuBufferCreate(s.frame, d, sizeof(FrameView), usage, true)) {
        meshletGpuDestroy(s, d);
        return false;
    }
    std::memcpy(s.verts.mapped, verts, sizeof(verts));
    std::memcpy(s.indices.mapped, indices, sizeof(indices));
    return true;
}

void meshletGpuDestroy(MeshletGpu& s, Device& d) {
    gpuBufferDestroy(s.frame, d);
    gpuBufferDestroy(s.indices, d);
    gpuBufferDestroy(s.verts, d);
}

void meshletGpuWriteFrame(MeshletGpu& s, VkExtent2D extent) {
    if (s.frame.mapped == nullptr || extent.width == 0 || extent.height == 0) {
        return;
    }
    CameraPose pose{};
    pose.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const CameraCull cam = cameraCullBuild(pose, nullptr);

    FrameView f{};
    std::memcpy(f.viewProj, cam.viewProj.m, sizeof(cam.viewProj.m));
    std::memcpy(f.invViewProj, cam.invViewProj.m, sizeof(cam.invViewProj.m));
    const float sx = 0.35f;
    const float sy = 0.85f;
    const float sz = 0.40f;
    const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    f.sunDirIntensity[0] = sx / sl;
    f.sunDirIntensity[1] = sy / sl;
    f.sunDirIntensity[2] = sz / sl;
    f.sunDirIntensity[3] = 3.2f;
    f.sunColor[0] = 1.0f;
    f.sunColor[1] = 0.96f;
    f.sunColor[2] = 0.90f;
    f.sunColor[3] = 1.0f;
    f.cameraPos[0] = cam.pos[0];
    f.cameraPos[1] = cam.pos[1];
    f.cameraPos[2] = cam.pos[2];
    f.invExtent[0] = 1.0f / static_cast<float>(extent.width);
    f.invExtent[1] = 1.0f / static_cast<float>(extent.height);
    f.extent[0] = extent.width;
    f.extent[1] = extent.height;
    std::memcpy(s.frame.mapped, &f, sizeof(f));
}

} // namespace burnhope
