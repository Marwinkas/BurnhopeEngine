#include "ui/rhi/UiRenderer.hpp"

#include "ui/PrimitiveBatch.hpp"
#include "image/ImageFile.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/MeshDraw.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

namespace burnhope {

bool uiRendererCreate(UiRenderer& r, Device& d, DescriptorHeaps& heaps) {
    uiRendererDestroy(r, d);
    r.device = &d;
    r.heaps = &heaps;
    r.nullMesh = d.caps.meshShader;

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(kUiFontBase) + kUiFontBytes;
    for (uint32_t flight = 0; flight < 2u; ++flight) {
        if (!gpuBufferCreate(
                r.prims[flight],
                d,
                bytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                true)) {
            spdlog::error("ui primitive buffer failed");
            uiRendererDestroy(r, d);
            return false;
        }
    }
    for (uint32_t flight = 0; flight < 2u; ++flight) {
        if (!heapWriteBufferFlight(heaps, d, flight, kUiDescPrims, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, r.prims[flight].address, r.prims[flight].size)) {
            spdlog::error("ui heap write failed");
            uiRendererDestroy(r, d);
            return false;
        }
    }

    uint8_t blank[4] = {28, 30, 36, 255};
    std::vector<uint8_t> demo(128u * 256u * 4u);
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 128; ++x) {
            uint8_t* px = demo.data() + (static_cast<size_t>(y) * 128u + x) * 4u;
            const bool second = y >= 128;
            const uint32_t ly = second ? y - 128 : y;
            px[0] = static_cast<uint8_t>(x * 2);
            px[1] = static_cast<uint8_t>(ly * 2);
            px[2] = second ? 210 : 48;
            px[3] = 255;
        }
    }
    bool photosOk = gpuImageUploadRgba(r.photos[0], d, demo.data(), 128, 256, 2);
    r.photoMeta[0].frames = 2;
    r.photoMeta[0].delayMs[0] = 420;
    r.photoMeta[0].delayMs[1] = 420;
    for (uint8_t slot = 1; slot < kPhotoSlots && photosOk; ++slot) {
        photosOk = gpuImageUploadRgba(r.photos[slot], d, blank, 1, 1);
        r.photoMeta[slot].frames = 1;
        r.photoMeta[slot].delayMs[0] = 100;
    }
    for (uint8_t slot = 0; slot < kPhotoSlots && photosOk; ++slot) {
        photosOk = heapWriteImage(
            heaps,
            d,
            kUiDescPhoto + slot,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            r.photos[slot],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    if (!photosOk) {
        spdlog::error("ui photo slots failed");
        uiRendererDestroy(r, d);
        return false;
    }

    VkDescriptorSetAndBindingMappingEXT vsMap = heapMap(
        0,
        0,
        VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(heaps, kUiDescPrims),
        static_cast<uint32_t>(heaps.bufferDescSize));
    VkDescriptorSetAndBindingMappingEXT fsMap[3];
    fsMap[0] = heapMap(
        0,
        1,
        VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(heaps, kUiDescFont),
        static_cast<uint32_t>(heaps.imageDescSize));
    fsMap[1] = heapMap(
        1,
        0,
        VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(heaps, 0),
        static_cast<uint32_t>(heaps.samplerDescSize));
    fsMap[2] = heapMap(
        0,
        2,
        VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(heaps, kUiDescPhoto),
        static_cast<uint32_t>(heaps.imageDescSize));
    const ShaderCreateDesc vs{
        .path = BH_UI_VERT,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 1,
        .mappings = &vsMap,
    };
    const ShaderCreateDesc fs{
        .path = BH_UI_FRAG,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 3,
        .mappings = fsMap,
    };
    if (!shaderCreate(d, vs, r.vs) || !shaderCreate(d, fs, r.fs)) {
        spdlog::error("ui shaders failed");
        uiRendererDestroy(r, d);
        return false;
    }
    if (d.caps.meshShader) {
        const ShaderCreateDesc ms{
            .path = BH_UI_MESH,
            .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
            .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .mappingCount = 1,
            .mappings = &vsMap,
        };
        if (!shaderCreate(d, ms, r.ms)) {
            spdlog::warn("ui mesh shader failed, falling back to vertex-pull path");
            r.ms = {};
        }
    }
    return true;
}

void uiRendererDestroy(UiRenderer& r, Device& d) {
    shaderDestroy(d, r.vs);
    shaderDestroy(d, r.fs);
    shaderDestroy(d, r.ms);
    gpuImageDestroy(r.font, d);
    for (uint8_t slot = 0; slot < kPhotoSlots; ++slot) {
        gpuImageDestroy(r.photos[slot], d);
        r.photoMeta[slot] = {};
    }
    gpuBufferDestroy(r.prims[0], d);
    gpuBufferDestroy(r.prims[1], d);
    r.primCount = 0;
    r.nullMesh = false;
    r.heaps = nullptr;
    r.device = nullptr;
}

void uiRendererUploadPrims(UiRenderer& r, Device& d, uint32_t flight, std::span<const UiPrimitive> prims, float viewW, float viewH) {
    if (flight >= 2u || r.prims[flight].mapped == nullptr) {
        return;
    }
    const uint32_t n = prims.size() > kUiPrimCap ? kUiPrimCap : static_cast<uint32_t>(prims.size());
    auto* bytes = static_cast<std::byte*>(r.prims[flight].mapped);
    auto* dst = reinterpret_cast<UiPrimitive*>(bytes + kUiHeaderBytes);
    if (n > 0 && prims.data() != dst) {
        std::memcpy(dst, prims.data(), sizeof(UiPrimitive) * n);
    }
    const UiGpuHeader hdr{viewW, viewH, n, 0};
    std::memcpy(bytes, &hdr, sizeof(hdr));
    r.primCount = n;
    gpuBufferFlush(r.prims[flight], d, 0, kUiHeaderBytes + sizeof(UiPrimitive) * n);
}

UiPrimitive* uiRendererPrimDst(UiRenderer& r, uint32_t flight, uint32_t& cap) {
    cap = 0;
    if (flight >= 2u || r.prims[flight].mapped == nullptr) {
        return nullptr;
    }
    cap = kUiPrimCap;
    return reinterpret_cast<UiPrimitive*>(static_cast<std::byte*>(r.prims[flight].mapped) + kUiHeaderBytes);
}

void uiRendererUploadFontTable(UiRenderer& r, Device& d, const UiFontAtlas& font) {
    for (uint32_t flight = 0; flight < 2u; ++flight) {
        if (r.prims[flight].mapped == nullptr) {
            continue;
        }
        auto* table = reinterpret_cast<float*>(static_cast<std::byte*>(r.prims[flight].mapped) + kUiFontBase);
        for (uint32_t i = 0; i < kUiFontGlyphs; ++i) {
            table[i * 4 + 0] = font.glyphs[i].u0;
            table[i * 4 + 1] = font.glyphs[i].v0;
            table[i * 4 + 2] = font.glyphs[i].u1;
            table[i * 4 + 3] = font.glyphs[i].v1;
        }
        gpuBufferFlush(r.prims[flight], d, kUiFontBase, kUiFontBytes);
    }
}

void uiRendererRecord(VkCommandBuffer cmd, const UiRenderer& r, const Swapchain& sc, const FrameContext& fc, uint32_t primCount, bool keepColor) {
    VkPipelineStageFlags2 primDst = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if (r.ms.handle != VK_NULL_HANDLE) {
        primDst |= VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
    }
    const uint32_t primFlight = fc.flight < 2u ? fc.flight : 0u;
    rhiBufferBarrier(
        cmd,
        r.prims[primFlight].buffer,
        0,
        r.prims[primFlight].size,
        VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_HOST_WRITE_BIT,
        primDst,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const bool alreadyColor = fc.imageIndex < 8 && sc.inColor[fc.imageIndex] != 0;
    rhiImageBarrier(
        cmd,
        sc.images[fc.imageIndex],
        (keepColor || alreadyColor) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        (keepColor || alreadyColor) ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkClearValue clear{};
    clear.color.float32[0] = 0.102f;
    clear.color.float32[1] = 0.106f;
    clear.color.float32[2] = 0.122f;
    clear.color.float32[3] = 1.0f;
    VkRenderingAttachmentInfo color{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = sc.views[fc.imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = keepColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear,
    };
    VkRenderingInfo ri{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, sc.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vkCmdBeginRendering(cmd, &ri);
    const bool mark = r.device != nullptr && r.device->validation;
    if (mark) {
        VkDebugUtilsLabelEXT label{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = "UI",
        };
        vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
    }
    if (primCount > 0) {
        cmdSetGraphicsDynamic(cmd, sc.extent, 1, false, true, sc.asked.transparent ? 1 : 0);
        const VkRect2D scissor{{0, 0}, sc.extent};
        vkCmdSetScissorWithCount(cmd, 1, &scissor);
        meshDrawRecord(cmd, MeshDrawDesc{
            .mesh = r.ms.handle,
            .vert = r.vs.handle,
            .frag = r.fs.handle,
            .vertNullMeshStage = r.nullMesh,
            .groupCount = primCount,
            .vertexCount = primCount * 6,
        });
    }
    if (mark) {
        vkCmdEndDebugUtilsLabelEXT(cmd);
    }
    vkCmdEndRendering(cmd);
}

} // namespace burnhope
