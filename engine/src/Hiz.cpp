#include "render/Hiz.hpp"
#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/PassShaderGroup.hpp"

#include "render/RenderDefs.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool hizPassCreate(HizPass& p, Device& d, const DescriptorHeaps& heaps) {
    hizPassDestroy(p, d);
    PassBindings maxBind{heaps};
    maxBind.ubo(0, HeapBuf::Frame);
    maxBind.sampledImg(2, HeapImg::Depth);
    maxBind.storageImg(6, HeapImg::HiZMax);
    maxBind.storageImg(33, HeapImg::HiZMax1);
    const ShaderCreateDesc desc{
        .path = BH_SHADER_DIR "/hizmax.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = maxBind.raw.count,
        .mappings = maxBind.raw.mappings,
        .pushBytes = 16,
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    if (!shaderCreate(d, desc, p.cs)) {
        spdlog::error("hiz max shader failed");
        hizPassDestroy(p, d);
        return false;
    }
    return true;
}

void hizPassDestroy(HizPass& p, Device& d) {
    shaderDestroy(d, p.cs);
}

void hizMaxBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const HizPass& p, TrackedImage& hiz, uint32_t flight) {
    struct HizPush {
        uint32_t flight;
        uint32_t mip;
        uint32_t pad0;
        uint32_t pad1;
    };
    for (uint32_t mip = 0; mip < 6; ++mip) {
        imageBarrier(cmd, hiz, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        cmdBindCompute(cmd, p.cs.handle);
        const HizPush push{flight & 1u, mip, 0, 0};
        vkCmdPushConstants(cmd, VK_NULL_HANDLE, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        dispatch2D(cmd, mipExtent(extent, mip), 8u);
        imageBarrier(cmd, hiz, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kPingAccess);
    }
    imageBarrier(cmd, hiz, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace burnhope
