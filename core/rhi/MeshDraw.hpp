#pragma once

#include "rhi/ShaderObject.hpp"

namespace burnhope {

// One registered draw pass (plan Фаза 1/5 contract): explicit shaders in, explicit draw
// count in, exactly one vkCmdDraw*/vkCmdDrawMeshTasksEXT out, no hidden global state between
// passes. This is the single point core/ui and engine/render both call instead of repeating
// cmdBindMeshFrag/cmdBindVertFrag + the draw call — a later render graph (Фаза 13) can be
// built out of a list of these without either side knowing about the other.
struct MeshDrawDesc {
    // Mesh-shader path (VK_EXT_mesh_shader). VK_NULL_HANDLE disables it — caller already
    // decided at shader-create time whether this GPU/pass has a mesh-shader variant.
    VkShaderEXT mesh = VK_NULL_HANDLE;
    // Vertex-pull fallback, used when `mesh` is VK_NULL_HANDLE (device lacks mesh shaders,
    // or this pass has no mesh variant at all). VK_NULL_HANDLE disables it too.
    VkShaderEXT vert = VK_NULL_HANDLE;
    VkShaderEXT frag = VK_NULL_HANDLE;
    // Vertex-pull path must still bind an explicit null mesh stage on devices that support
    // mesh shaders (spec requirement) — same flag Canvas.cpp already carried as `nullMesh`.
    bool vertNullMeshStage = false;
    // Mesh path: workgroup count passed to vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1).
    uint32_t groupCount = 0;
    VkBuffer indirect = VK_NULL_HANDLE;
    VkDeviceSize indirectOffset = 0;
    // Vertex-pull path: raw vertex count passed to vkCmdDraw(cmd, vertexCount, 1, 0, 0).
    uint32_t vertexCount = 0;
    // Optional push before the one draw. Visbuffer puts the instance BDA here.
    const void* pushData = nullptr;
    uint32_t pushBytes = 0;
    VkPipelineLayout pushLayout = VK_NULL_HANDLE;
    VkShaderStageFlags pushStages = 0;
};

// Binds shaders and issues exactly one draw for this pass: mesh-shader groups if `mesh` is
// set and `groupCount > 0`, else the vertex-pull fallback if `vert` is set and
// `vertexCount > 0`, else nothing. Caller has already begun dynamic rendering and called
// cmdSetGraphicsDynamic — this only binds shaders and draws, no render-pass/pipeline state.
void meshDrawRecord(VkCommandBuffer cmd, const MeshDrawDesc& desc);

} // namespace burnhope
