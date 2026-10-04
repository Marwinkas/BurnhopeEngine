#include "render/Cube.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapMaps.hpp"
#include "render/PassRendering.hpp"
#include "render/PassShaderGroup.hpp"
#include "render/PassTransitions.hpp"
#include "rhi/MeshDraw.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool cubePassCreate(CubePass& p, Device& d, const DescriptorHeaps& heaps) {
    cubePassDestroy(p, d);
    const char* path[6] = {
        BH_SHADER_DIR "/cube0.mesh.spv",
        BH_SHADER_DIR "/cube1.mesh.spv",
        BH_SHADER_DIR "/cube2.mesh.spv",
        BH_SHADER_DIR "/cube3.mesh.spv",
        BH_SHADER_DIR "/cube4.mesh.spv",
        BH_SHADER_DIR "/cube5.mesh.spv",
    };
    VkDescriptorSetAndBindingMappingEXT maps[6][9];
    if (!createShaderGroup(d, p.mesh, path, VK_SHADER_STAGE_MESH_BIT_EXT, [&](uint32_t face, ShaderCreateDesc& desc) {
            heapMapsCube(heaps, maps[face], face);
            desc.mappingCount = 9;
            desc.mappings = maps[face];
        }, VK_SHADER_STAGE_FRAGMENT_BIT)) {
        spdlog::error("cube mesh shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    heapMapsCube(heaps, maps[0], 0);
    const ShaderCreateDesc frag{
        .path = BH_SHADER_DIR "/cube.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 8,
        .mappings = maps[0],
    };
    if (!shaderCreate(d, frag, p.frag)) {
        spdlog::error("cube fragment shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    return true;
}

void cubePassDestroy(CubePass& p, Device& d) {
    for (uint32_t face = 0; face < 6; ++face) {
        shaderDestroy(d, p.mesh[face]);
    }
    shaderDestroy(d, p.frag);
}

void cubePassRecord(VkCommandBuffer cmd, const CubePass& p, const GpuImage& color, const GpuImage& depth, const uint32_t counts[6]) {
    transitionToColorAttachment(cmd, color.image);
    transitionToDepthAttachment(cmd, depth.image);
    VkClearValue colorClear{};
    colorClear.color.float32[0] = 0.45f;
    colorClear.color.float32[1] = 0.55f;
    colorClear.color.float32[2] = 0.7f;
    colorClear.color.float32[3] = 1.0f;
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 1.0f;
    RenderingPass rendering = beginRenderingColorDepth(cmd, color.extent, color.view, depth.view, colorClear, depthClear, true);
    cmdSetGraphicsDynamic(cmd, color.extent, 1, true);
    for (uint32_t face = 0; face < 6; ++face) {
        if (counts[face] == 0) {
            continue;
        }
        meshDrawRecord(cmd, MeshDrawDesc{
            .mesh = p.mesh[face].handle,
            .frag = p.frag.handle,
            .groupCount = counts[face],
        });
    }
    endRendering(rendering);
    transitionAttachmentToSampled(cmd, color.image, VK_IMAGE_ASPECT_COLOR_BIT);
}

} // namespace burnhope
