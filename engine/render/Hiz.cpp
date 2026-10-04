#include "render/Hiz.hpp"
#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/PassShaderGroup.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif

#include <spdlog/spdlog.h>

namespace burnhope {

bool hizPassCreate(HizPass& p, Device& d, const DescriptorHeaps& heaps) {
    hizPassDestroy(p, d);
    const char* hizMaxPath[6] = {
        BH_SHADER_DIR "/hizmax0.comp.spv",
        BH_SHADER_DIR "/hizmax1.comp.spv",
        BH_SHADER_DIR "/hizmax2.comp.spv",
        BH_SHADER_DIR "/hizmax3.comp.spv",
        BH_SHADER_DIR "/hizmax4.comp.spv",
        BH_SHADER_DIR "/hizmax5.comp.spv",
    };
    bool ok = true;
    for (uint32_t flight = 0; ok && flight < 2; ++flight) {
        PassBindings maxBind{heaps};
        maxBind.ubo(0, HeapBuf::Frame);
        maxBind.sampledImg(2, HeapImg::Depth);
        maxBind.storageImg(6, flight == 0u ? static_cast<uint32_t>(HeapImg::HiZMax) : static_cast<uint32_t>(HeapImg::HiZMax1));
        ok = createShaderArray(d, p.hizMax[flight], hizMaxPath, VK_SHADER_STAGE_COMPUTE_BIT, maxBind.raw.mappings, 3u);
    }
    if (!ok) {
        spdlog::error("hiz max shader failed");
        hizPassDestroy(p, d);
        return false;
    }
    return true;
}

void hizPassDestroy(HizPass& p, Device& d) {
    for (uint32_t flight = 0; flight < 2; ++flight) {
        destroyShaderArray(d, p.hizMax[flight]);
    }
}

void hizMaxBuildRecord(VkCommandBuffer cmd, VkExtent2D extent, const HizPass& p, TrackedImage& hiz, uint32_t flight) {
    dispatchMipChainDown(cmd, p.hizMax[flight & 1u], 6, extent, hiz);
    imageBarrier(cmd, hiz, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace burnhope
