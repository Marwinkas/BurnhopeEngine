#include "render/Restir.hpp"

#include "render/HeapBind.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr VkDeviceSize kRisPayload = VkDeviceSize{kRestirTileSize} * kRestirTileCount * sizeof(uint32_t) * 2u;
constexpr VkDeviceSize kRisStatBytes = 256;
constexpr VkDeviceSize kRisFlight = kRisPayload + kRisStatBytes;

uint32_t integerCompact(uint32_t x) {
    x = (x & 0x11111111u) | ((x & 0x44444444u) >> 1);
    x = (x & 0x03030303u) | ((x & 0x30303030u) >> 2);
    x = (x & 0x000F000Fu) | ((x & 0x0F000F00u) >> 4);
    x = (x & 0x000000FFu) | ((x & 0x00FF0000u) >> 8);
    return x;
}

void linearToZCurve(uint32_t index, uint32_t& x, uint32_t& y) {
    x = integerCompact(index);
    y = integerCompact(index >> 1);
}

float lightPower(const LightGpuData& light, uint32_t passMask) {
    if (light.type == kLightDirectional || light.radius <= 0.0f) {
        return 0.0f;
    }
    if (light.type == kLightPoint && (passMask & 512u) == 0u) {
        return 0.0f;
    }
    if (light.type == kLightSpot && (passMask & 65536u) == 0u) {
        return 0.0f;
    }
    if ((light.flags & 1u) != 0u && (passMask & 131072u) == 0u) {
        return 0.0f;
    }
    if (light.type != kLightPoint && light.type != kLightSpot && (light.flags & 1u) == 0u) {
        return 0.0f;
    }
    const float luma = 0.2126f * light.color[0] + 0.7152f * light.color[1] + 0.0722f * light.color[2];
    return luma * std::max(light.intensity, 0.0f);
}

void tuneContext(rtxdi::ReSTIRDIContext& ctx) {
    RTXDI_DIInitialSamplingParameters initial = ctx.GetInitialSamplingParameters();
    initial.numLocalLightSamples = 8;
    initial.numInfiniteLightSamples = 0;
    initial.numEnvironmentSamples = 0;
    initial.numBrdfSamples = 0;
    initial.enableInitialVisibility = 0;
    initial.environmentMapImportanceSampling = 0;
    initial.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Power_RIS;
    ctx.SetInitialSamplingParameters(initial);

    RTXDI_DISpatialResamplingParameters spatial = ctx.GetSpatialResamplingParameters();
    spatial.numSamples = 3;
    spatial.numDisocclusionBoostSamples = 3;
    spatial.samplingRadius = 4.0f;
    spatial.depthThreshold = 0.1f;
    spatial.normalThreshold = 0.9f;
    spatial.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
    ctx.SetSpatialResamplingParameters(spatial);

    RTXDI_DITemporalResamplingParameters temporal = ctx.GetTemporalResamplingParameters();
    temporal.maxHistoryLength = 20;
    temporal.depthThreshold = 0.1f;
    temporal.normalThreshold = 0.9f;
    temporal.enableVisibilityShortcut = 0;
    temporal.biasCorrectionMode = ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    ctx.SetTemporalResamplingParameters(temporal);

    ctx.SetResamplingMode(rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial);
    ctx.SetFrameIndex(0);
}

bool makeContext(RestirPass& p, VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }
    rtxdi::ReSTIRDIStaticParameters params{};
    params.NeighborOffsetCount = kRestirNeighborCount;
    params.RenderWidth = extent.width;
    params.RenderHeight = extent.height;
    params.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Off;
    for (uint32_t flight = 0; flight < 2u; ++flight) {
        p.ctx[flight].emplace(params);
        tuneContext(*p.ctx[flight]);
        p.parity[flight] = 0;
        p.historyValid[flight] = 0;
        std::memset(p.prevViewProj[flight], 0, sizeof(p.prevViewProj[flight]));
        std::memset(p.prevInvViewProj[flight], 0, sizeof(p.prevInvViewProj[flight]));
        std::memset(p.prevCamera[flight], 0, sizeof(p.prevCamera[flight]));
    }
    p.width = extent.width;
    p.height = extent.height;
    p.cleared = false;
    return true;
}

void destroyScreen(RestirPass& p, Device& d) {
    gpuBufferDestroy(p.reservoirs, d);
    gpuImageDestroy(p.histPos, d);
    gpuImageDestroy(p.histMat, d);
    p.histPosLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.histMatLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.cleared = false;
    p.statReady[0] = 0;
    p.statReady[1] = 0;
    p.ctx[0].reset();
    p.ctx[1].reset();
}

bool createScreen(RestirPass& p, Device& d, VkExtent2D extent) {
    destroyScreen(p, d);
    if (!makeContext(p, extent)) {
        return false;
    }
    const RTXDI_ReservoirBufferParameters reservoir = p.ctx[0]->GetReservoirBufferParameters();
    const VkDeviceSize bytes = VkDeviceSize{reservoir.reservoirArrayPitch} * 3u * 2u * sizeof(RestirSlot);
    const VkExtent2D hist{extent.width * 2u, extent.height * 2u};
    const bool ok = gpuBufferCreate(p.reservoirs, d, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false)
        && gpuImageCreate(p.histPos, d, hist, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
        && gpuImageCreate(p.histMat, d, hist, VK_FORMAT_R32G32B32A32_UINT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!ok) {
        spdlog::error("restir screen buffers failed");
        destroyScreen(p, d);
        return false;
    }
    return true;
}

bool shaderOf(Device& d, const PassBindings& bind, const char* path, ShaderExt& out) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
        .entry = "main",
        .pushBytes = sizeof(RestirPush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    return shaderCreate(d, desc, out);
}

void imageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout& layout, VkImageLayout next, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, uint32_t mips) {
    const VkImageLayout old = layout;
    VkPipelineStageFlags2 stage = srcStage;
    VkAccessFlags2 access = srcAccess;
    if (old == VK_IMAGE_LAYOUT_UNDEFINED) {
        stage = VK_PIPELINE_STAGE_2_NONE;
        access = VK_ACCESS_2_NONE;
    }
    rhiImageBarrierMips(cmd, image, old, next, stage, access, dstStage, dstAccess, 0, mips);
    layout = next;
}

void clearHistory(VkCommandBuffer cmd, RestirPass& p) {
    imageLayout(cmd, p.histPos.image, p.histPosLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1);
    imageLayout(cmd, p.histMat.image, p.histMatLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1);
    const VkClearColorValue zero{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, p.histPos.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    vkCmdClearColorImage(cmd, p.histMat.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    imageLayout(cmd, p.histPos.image, p.histPosLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 1);
    imageLayout(cmd, p.histMat.image, p.histMatLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 1);
}

void splitLights(const LightGpuData* lights, uint32_t count, uint32_t& localFirst, uint32_t& localCount, uint32_t& infFirst, uint32_t& infCount) {
    localFirst = 0;
    localCount = 0;
    infFirst = 0;
    infCount = 0;
    int localA = -1;
    int localB = -1;
    int infA = -1;
    int infB = -1;
    bool localGap = false;
    bool infGap = false;
    for (uint32_t i = 0; i < count; ++i) {
        const bool infinite = lights[i].type == kLightDirectional;
        const bool local = lights[i].type == kLightPoint || lights[i].type == kLightSpot || (lights[i].flags & 1u) != 0u;
        if (infinite) {
            if (infA < 0) {
                infA = static_cast<int>(i);
            } else if (static_cast<int>(i) != infB) {
                infGap = true;
            }
            infB = static_cast<int>(i) + 1;
        } else if (local) {
            if (localA < 0) {
                localA = static_cast<int>(i);
            } else if (static_cast<int>(i) != localB) {
                localGap = true;
            }
            localB = static_cast<int>(i) + 1;
        }
    }
    if (!localGap && localA >= 0) {
        localFirst = static_cast<uint32_t>(localA);
        localCount = static_cast<uint32_t>(localB - localA);
    }
    if (!infGap && infA >= 0) {
        infFirst = static_cast<uint32_t>(infA);
        infCount = static_cast<uint32_t>(infB - infA);
    }
}

void fillPdf(RestirPass& p, const LightGpuData* lights, uint32_t lightCount, uint32_t passMask) {
    std::fill(p.pdfCpu.begin(), p.pdfCpu.end(), 0.0f);
    p.powered = 0;
    p.powerSum = 0.0f;
    const uint32_t count = std::min(lightCount, kLightCap);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t x = 0;
        uint32_t y = 0;
        linearToZCurve(i, x, y);
        const float power = lightPower(lights[i], passMask);
        if (power > 0.0f) {
            p.powered += 1u;
            p.powerSum += power;
        }
        if (x < p.pdfW && y < p.pdfH) {
            p.pdfCpu[y * p.pdfW + x] = power;
        }
    }
    uint32_t src = 0;
    uint32_t mw = p.pdfW;
    uint32_t mh = p.pdfH;
    for (uint32_t mip = 1; mip < p.pdfMips; ++mip) {
        const uint32_t nw = std::max(mw / 2u, 1u);
        const uint32_t nh = std::max(mh / 2u, 1u);
        const uint32_t dst = src + mw * mh;
        for (uint32_t y = 0; y < nh; ++y) {
            for (uint32_t x = 0; x < nw; ++x) {
                float sum = 0.0f;
                for (uint32_t oy = 0; oy < 2u; ++oy) {
                    for (uint32_t ox = 0; ox < 2u; ++ox) {
                        const uint32_t sx = std::min(x * 2u + ox, mw - 1u);
                        const uint32_t sy = std::min(y * 2u + oy, mh - 1u);
                        sum += p.pdfCpu[src + sy * mw + sx];
                    }
                }
                p.pdfCpu[dst + y * nw + x] = sum * 0.25f;
            }
        }
        src = dst;
        mw = nw;
        mh = nh;
    }
}

void uploadPdf(VkCommandBuffer cmd, RestirPass& p, Device& d, uint32_t flight) {
    const VkDeviceSize bytes = VkDeviceSize{p.pdfFloats} * sizeof(float);
    auto* dst = static_cast<uint8_t*>(p.pdfStaging.mapped) + bytes * flight;
    std::memcpy(dst, p.pdfCpu.data(), bytes);
    gpuBufferFlush(p.pdfStaging, d, bytes * flight, bytes);
    rhiBufferBarrier(cmd, p.pdfStaging.buffer, bytes * flight, bytes,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    imageLayout(cmd, p.pdf[flight].image, p.pdfLayout[flight], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, p.pdfMips);
    VkBufferImageCopy regions[16]{};
    uint32_t mw = p.pdfW;
    uint32_t mh = p.pdfH;
    uint32_t offset = 0;
    for (uint32_t mip = 0; mip < p.pdfMips && mip < 16u; ++mip) {
        regions[mip].bufferOffset = bytes * flight + VkDeviceSize{offset} * sizeof(float);
        regions[mip].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
        regions[mip].imageExtent = {mw, mh, 1};
        offset += mw * mh;
        mw = std::max(mw / 2u, 1u);
        mh = std::max(mh / 2u, 1u);
    }
    vkCmdCopyBufferToImage(cmd, p.pdfStaging.buffer, p.pdf[flight].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, p.pdfMips, regions);
    imageLayout(cmd, p.pdf[flight].image, p.pdfLayout[flight], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, p.pdfMips);
}

void bindCompute(VkCommandBuffer cmd, const ShaderExt& shader, RestirPush push) {
    cmdBindCompute(cmd, shader.handle);
    vkCmdPushConstants(cmd, VK_NULL_HANDLE, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
}

uint32_t statAt(const uint8_t* base, uint32_t slot) {
    uint32_t value = 0;
    std::memcpy(&value, base + static_cast<size_t>(slot) * 8u, sizeof(value));
    return value;
}

void publishLog(RestirPass& p, uint32_t active, uint32_t lights, uint32_t powered, uint32_t tlas, uint32_t alive, uint32_t skip, const char* text) {
    std::snprintf(p.lastLog, sizeof(p.lastLog), "%s", text);
    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const bool same = active == p.logActive && lights == p.logLights && powered == p.logPowered && tlas == p.logTlas && alive == p.logAlive && skip == p.logSkip;
    if (same && p.logMs != 0u && now - p.logMs < 2000u) {
        return;
    }
    p.logMs = now;
    p.logActive = active;
    p.logLights = lights;
    p.logPowered = powered;
    p.logTlas = tlas;
    p.logAlive = alive;
    p.logSkip = skip;
    spdlog::info("{}", p.lastLog);
}

ReSTIRDI_TemporalBiasCorrectionMode temporalBias(uint32_t mode) {
    if (mode >= 2u) {
        return ReSTIRDI_TemporalBiasCorrectionMode::Raytraced;
    }
    if (mode == 1u) {
        return ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    }
    return ReSTIRDI_TemporalBiasCorrectionMode::Off;
}

ReSTIRDI_SpatialBiasCorrectionMode spatialBias(uint32_t mode) {
    if (mode >= 3u) {
        return ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
    }
    if (mode == 2u) {
        return ReSTIRDI_SpatialBiasCorrectionMode::Pairwise;
    }
    if (mode == 1u) {
        return ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    }
    return ReSTIRDI_SpatialBiasCorrectionMode::Off;
}

void storageBarrier(VkCommandBuffer cmd) {
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    };
    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void reservoirBarrier(VkCommandBuffer cmd, const RestirPass& p) {
    rhiBufferBarrier(cmd, p.reservoirs.buffer, 0, p.reservoirs.size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

} // namespace

bool restirCreate(RestirPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    restirDestroy(p, d);
    if (!d.caps.rayQuery) {
        spdlog::info("restir: ray query is off");
        return true;
    }
    PassBindings presample{heaps};
    presample.ubo(15, HeapBuf::RestirConst);
    presample.sampledImg(18, HeapImg::LightPdf);
    presample.storageBuf(14, HeapBuf::RestirRis, false);
    PassBindings di{heaps};
    di.ubo(0, HeapBuf::Frame);
    di.sun();
    di.sampledImg(2, HeapImg::Depth);
    di.storageImg(3, HeapImg::HdrAStorage);
    di.storageImg(4, HeapImg::Surf0);
    di.storageImg(5, HeapImg::Surf1);
    di.storageBuf(11, HeapBuf::RestirReservoir, false);
    di.storageBuf(12, HeapBuf::RestirNeighbor, true);
    di.storageBuf(14, HeapBuf::RestirRis, false);
    di.ubo(15, HeapBuf::RestirConst);
    di.storageImg(16, HeapImg::RestirPos);
    di.storageImg(17, HeapImg::RestirMat);
    di.storageBuf(19, HeapBuf::Lights, true);
    di.knob(50, HeapBuf::Flags);
    di.knob(55, HeapBuf::Shade);
    if (!shaderOf(d, presample, BH_SHADER_DIR "/restir.presample.comp.spv", p.presample)
        || !shaderOf(d, di, BH_SHADER_DIR "/restir.di.comp.spv", p.di)) {
        spdlog::error("restir shaders failed");
        restirDestroy(p, d);
        return false;
    }
    rtxdi::ComputePdfTextureSize(kLightCap, p.pdfW, p.pdfH, p.pdfMips);
    p.pdfFloats = 0;
    uint32_t mw = p.pdfW;
    uint32_t mh = p.pdfH;
    for (uint32_t mip = 0; mip < p.pdfMips; ++mip) {
        p.pdfFloats += mw * mh;
        mw = std::max(mw / 2u, 1u);
        mh = std::max(mh / 2u, 1u);
    }
    p.pdfCpu.assign(p.pdfFloats, 0.0f);
    const VkDeviceSize pdfBytes = VkDeviceSize{p.pdfFloats} * sizeof(float);
    const VkExtent2D pdfExtent{p.pdfW, p.pdfH};
    uint8_t raw[kRestirNeighborCount * 2u];
    rtxdi::FillNeighborOffsetBuffer(raw, kRestirNeighborCount);
    const bool buffers = gpuBufferCreate(p.neighbors, d, sizeof(float) * 2u * kRestirNeighborCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true)
        && gpuBufferCreate(p.ris, d, kRisFlight * 2u,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false)
        && gpuBufferCreate(p.statRead, d, kRisStatBytes * 2u,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true)
        && gpuBufferCreate(p.constants, d, kRestirConstStride * 2u,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true)
        && gpuBufferCreate(p.pdfStaging, d, pdfBytes * 2u,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true)
        && gpuImageCreate(p.pdf[0], d, pdfExtent, VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, p.pdfMips)
        && gpuImageCreate(p.pdf[1], d, pdfExtent, VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, p.pdfMips);
    if (!buffers || p.neighbors.mapped == nullptr || p.statRead.mapped == nullptr) {
        spdlog::error("restir light buffers failed");
        restirDestroy(p, d);
        return false;
    }
    auto* neighbor = static_cast<float*>(p.neighbors.mapped);
    for (uint32_t i = 0; i < kRestirNeighborCount; ++i) {
        const auto sx = static_cast<int8_t>(raw[i * 2u]);
        const auto sy = static_cast<int8_t>(raw[i * 2u + 1u]);
        neighbor[i * 2u] = std::max(static_cast<float>(sx) / 127.0f, -1.0f);
        neighbor[i * 2u + 1u] = std::max(static_cast<float>(sy) / 127.0f, -1.0f);
    }
    gpuBufferFlush(p.neighbors, d, 0, p.neighbors.size);
    if (!createScreen(p, d, extent)) {
        restirDestroy(p, d);
        return false;
    }
    p.logActive = 0xffffffffu;
    spdlog::info(
        "ReSTIR DI готов {}x{} pdf {}x{} mip {} RIS {}x{} соседей {} резервуар {:.1f} МБ. Дебаг: 15 свет, 16 вес, 17 M, 18 тень, 19 индекс",
        p.width, p.height, p.pdfW, p.pdfH, p.pdfMips, kRestirTileSize, kRestirTileCount, kRestirNeighborCount,
        static_cast<double>(p.reservoirs.size) / 1048576.0);
    return true;
}

void restirDestroy(RestirPass& p, Device& d) {
    destroyScreen(p, d);
    shaderDestroy(d, p.presample);
    shaderDestroy(d, p.di);
    gpuBufferDestroy(p.neighbors, d);
    gpuBufferDestroy(p.ris, d);
    gpuBufferDestroy(p.statRead, d);
    gpuBufferDestroy(p.constants, d);
    gpuBufferDestroy(p.pdfStaging, d);
    gpuImageDestroy(p.pdf[0], d);
    gpuImageDestroy(p.pdf[1], d);
    p.pdfCpu.clear();
    p.pdfW = 0;
    p.pdfH = 0;
    p.pdfMips = 0;
    p.pdfFloats = 0;
    p.pdfLayout[0] = VK_IMAGE_LAYOUT_UNDEFINED;
    p.pdfLayout[1] = VK_IMAGE_LAYOUT_UNDEFINED;
    p.width = 0;
    p.height = 0;
}

bool restirResize(RestirPass& p, Device& d, VkExtent2D extent) {
    if (p.di.handle == VK_NULL_HANDLE) {
        return true;
    }
    return createScreen(p, d, extent);
}

bool restirBind(RestirPass& p, Device& d, DescriptorHeaps& heaps) {
    if (p.di.handle == VK_NULL_HANDLE) {
        return true;
    }
    if (p.reservoirs.address == 0 || p.neighbors.address == 0 || p.ris.address == 0 || p.constants.address == 0) {
        return false;
    }
    const bool shared = heapWriteStorage(heaps, d, HeapBuf::RestirReservoir, p.reservoirs)
        && heapWriteStorage(heaps, d, HeapBuf::RestirNeighbor, p.neighbors);
    bool flights = shared;
    for (uint32_t flight = 0; flights && flight < 2u; ++flight) {
        flights = heapWriteBufferFlight(heaps, d, flight, static_cast<uint32_t>(HeapBuf::RestirRis),
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, p.ris.address + kRisFlight * flight, kRisFlight)
            && heapWriteBufferFlight(heaps, d, flight, static_cast<uint32_t>(HeapBuf::RestirConst),
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, p.constants.address + kRestirConstStride * flight, sizeof(RestirConstants))
            && heapWriteImageFlight(heaps, d, flight, static_cast<uint32_t>(HeapImg::LightPdf),
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, p.pdf[flight], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, p.pdfMips);
    }
    const bool history = flights
        && heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::RestirPos), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            p.histPos, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1)
        && heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::RestirMat), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            p.histMat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    if (!history) {
        spdlog::error("restir descriptor bind failed");
    }
    return history;
}

void restirNote(RestirPass& p, uint32_t profile, const char* why) {
    char line[320];
    std::snprintf(line, sizeof(line), "ReSTIR выкл профиль %u (%s)", profile, why != nullptr ? why : "");
    publishLog(p, 0u, 0u, 0u, 0u, 0u, 3u + profile, line);
}

void restirRecord(
    VkCommandBuffer cmd,
    RestirPass& p,
    Device& d,
    const FrameView& view,
    const LightGpuData* lights,
    uint32_t lightCount,
    uint32_t passMask,
    uint32_t flight,
    VkExtent2D extent,
    VkDeviceAddress tlas) {
    if (p.di.handle == VK_NULL_HANDLE || p.presample.handle == VK_NULL_HANDLE || flight > 1u || !p.ctx[flight].has_value()) {
        publishLog(p, 0u, 0u, 0u, 0u, 0u, 1u, "ReSTIR пропуск: нет шейдера или контекста");
        return;
    }
    if (extent.width != p.width || extent.height != p.height || lights == nullptr) {
        publishLog(p, 0u, lightCount, 0u, 0u, 0u, 2u, "ReSTIR пропуск: размер кадра не совпал или список света пуст");
        return;
    }
    uint32_t surf = 0;
    uint32_t valid = 0;
    uint32_t vis = 0;
    uint32_t shadow = 0;
    uint32_t mSum = 0;
    uint32_t weightMilli = 0;
    if (p.statReady[flight] != 0u && p.statRead.mapped != nullptr) {
        gpuBufferInvalidate(p.statRead, d, kRisStatBytes * flight, kRisStatBytes);
        const auto* base = static_cast<const uint8_t*>(p.statRead.mapped) + kRisStatBytes * flight;
        surf = statAt(base, 0u);
        valid = statAt(base, 1u);
        vis = statAt(base, 2u);
        shadow = statAt(base, 3u);
        mSum = statAt(base, 4u);
        weightMilli = statAt(base, 5u);
    }
    if (!p.cleared) {
        vkCmdFillBuffer(cmd, p.reservoirs.buffer, 0, VK_WHOLE_SIZE, 0u);
        rhiBufferBarrier(cmd, p.reservoirs.buffer, 0, p.reservoirs.size,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        clearHistory(cmd, p);
        p.cleared = true;
    }
    fillPdf(p, lights, lightCount, passMask);
    uploadPdf(cmd, p, d, flight);
    uint32_t localFirst = 0;
    uint32_t localCount = 0;
    uint32_t infFirst = 0;
    uint32_t infCount = 0;
    splitLights(lights, lightCount, localFirst, localCount, infFirst, infCount);
    (void)infFirst;

    rtxdi::ReSTIRDIContext& ctx = *p.ctx[flight];
    rtxdi::ReSTIRDI_ResamplingMode mode = rtxdi::ReSTIRDI_ResamplingMode::None;
    if (p.knobTemporalOn != 0u && p.knobSpatialOn != 0u) {
        mode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
    } else if (p.knobTemporalOn != 0u) {
        mode = rtxdi::ReSTIRDI_ResamplingMode::Temporal;
    } else if (p.knobSpatialOn != 0u) {
        mode = rtxdi::ReSTIRDI_ResamplingMode::Spatial;
    }
    ctx.SetResamplingMode(mode);
    RestirConstants constants{};
    constants.runtime = ctx.GetRuntimeParams();
    constants.reservoir = ctx.GetReservoirBufferParameters();
    constants.indices = ctx.GetBufferIndices();
    constants.initial = ctx.GetInitialSamplingParameters();
    constants.temporal = ctx.GetTemporalResamplingParameters();
    constants.spatial = ctx.GetSpatialResamplingParameters();
    constants.boiling = ctx.GetBoilingFilterParameters();
    constants.lights.localLightBufferRegion.firstLightIndex = localFirst;
    constants.lights.localLightBufferRegion.numLights = localCount;
    constants.lights.infiniteLightBufferRegion.firstLightIndex = 0;
    constants.lights.infiniteLightBufferRegion.numLights = 0;
    constants.initial.numLocalLightSamples = localCount > 0u ? p.knobLocal : 0u;
    constants.initial.numInfiniteLightSamples = 0;
    constants.initial.enableInitialVisibility = p.knobInitVis;
    constants.temporal.maxHistoryLength = p.knobHistory;
    constants.temporal.depthThreshold = p.knobTemporalDepth;
    constants.temporal.normalThreshold = p.knobTemporalNormal;
    constants.temporal.biasCorrectionMode = temporalBias(p.knobTemporalBias);
    constants.temporal.enablePermutationSampling = p.knobPerm;
    constants.temporal.permutationSamplingThreshold = p.knobPermTh;
    constants.temporal.enableVisibilityShortcut = p.knobShortcut;
    constants.spatial.numSamples = p.knobSpatialOn != 0u ? p.knobSpatial : 0u;
    constants.spatial.numDisocclusionBoostSamples = p.knobSpatialOn != 0u ? p.knobSpatialBoost : 0u;
    constants.spatial.samplingRadius = p.knobRadius;
    constants.spatial.depthThreshold = p.knobSpatialDepth;
    constants.spatial.normalThreshold = p.knobSpatialNormal;
    constants.spatial.targetHistoryLength = p.knobTarget;
    constants.spatial.biasCorrectionMode = spatialBias(p.knobSpatialBias);
    constants.spatial.enableMaterialSimilarityTest = p.knobMaterial;
    constants.spatial.discountNaiveSamples = p.knobNaive;
    constants.boiling.boilingFilterStrength = p.knobBoil;
    constants.boiling.enableBoilingFilter = p.knobBoilOn != 0u && p.knobBoil > 0.0f ? 1u : 0u;
    constants.localRis.bufferOffset = 0;
    constants.localRis.tileSize = kRestirTileSize;
    constants.localRis.tileCount = kRestirTileCount;
    constants.envRis.bufferOffset = 0;
    constants.envRis.tileSize = 1;
    constants.envRis.tileCount = 1;
    std::memcpy(constants.prevViewProj, p.prevViewProj[flight], sizeof(constants.prevViewProj));
    std::memcpy(constants.prevInvViewProj, p.prevInvViewProj[flight], sizeof(constants.prevInvViewProj));
    std::memcpy(constants.prevCamera, p.prevCamera[flight], sizeof(constants.prevCamera));
    constants.historyValid = p.historyValid[flight];
    constants.readSlice = flight * 2u + p.parity[flight];
    constants.writeSlice = flight * 2u + (1u - p.parity[flight]);
    constants.pdfWidth = p.pdfW;
    constants.pdfHeight = p.pdfH;
    constants.tlasLo = static_cast<uint32_t>(tlas);
    constants.tlasHi = static_cast<uint32_t>(tlas >> 32);
    constants.reservoirBase = flight * 3u;
    constants.lightCount = localCount;
    constants.sunDisk = p.knobSunDisk;
    constants.shadowRange = p.knobShadowRange;
    constants.shadowBias = p.knobShadowBias;
    constants.shadowRays = p.knobShadowRays;
    constants.pad1 = p.knobTemporalOn;
    constants.pad2 = p.knobSunRays;
    auto* dst = static_cast<uint8_t*>(p.constants.mapped) + kRestirConstStride * flight;
    std::memcpy(dst, &constants, sizeof(constants));
    gpuBufferFlush(p.constants, d, kRestirConstStride * flight, sizeof(constants));
    rhiBufferBarrier(cmd, p.constants.buffer, kRestirConstStride * flight, sizeof(constants),
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    rhiBufferBarrier(cmd, p.neighbors.buffer, 0, p.neighbors.size,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

    imageLayout(cmd, p.histPos.image, p.histPosLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 1);
    imageLayout(cmd, p.histMat.image, p.histMatLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 1);

    RestirPush push{};
    if (localCount > 0u) {
        bindCompute(cmd, p.presample, push);
        vkCmdDispatch(cmd, kRestirTileSize / 256u, kRestirTileCount, 1u);
    }
    rhiBufferBarrier(cmd, p.ris.buffer, kRisFlight * flight, kRisPayload,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    push.pass = 0;
    bindCompute(cmd, p.di, push);
    dispatch2D(cmd, extent, 8u);
    reservoirBarrier(cmd, p);

    if (p.knobTemporalOn != 0u) {
        push.pass = 1;
        bindCompute(cmd, p.di, push);
        dispatch2D(cmd, extent, 8u);
        reservoirBarrier(cmd, p);
    }

    if (p.knobSpatialOn != 0u) {
        push.pass = 2;
        bindCompute(cmd, p.di, push);
        dispatch2D(cmd, extent, 8u);
        reservoirBarrier(cmd, p);
    }

    const VkDeviceSize statOff = kRisFlight * flight + kRisPayload;
    vkCmdFillBuffer(cmd, p.ris.buffer, statOff, kRisStatBytes, 0u);
    rhiBufferBarrier(cmd, p.ris.buffer, statOff, kRisStatBytes,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    push.pass = 3;
    bindCompute(cmd, p.di, push);
    dispatch2D(cmd, extent, 8u);
    if (p.knobSunOn != 0u) {
        storageBarrier(cmd);
        push.pass = 4;
        bindCompute(cmd, p.di, push);
        dispatch2D(cmd, extent, 8u);
    }
    rhiBufferBarrier(cmd, p.ris.buffer, statOff, kRisStatBytes,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferCopy statCopy{
        .srcOffset = statOff,
        .dstOffset = kRisStatBytes * flight,
        .size = kRisStatBytes,
    };
    vkCmdCopyBuffer(cmd, p.ris.buffer, p.statRead.buffer, 1u, &statCopy);
    p.statReady[flight] = 1u;

    const uint32_t hist = p.historyValid[flight];
    const float mAvg = valid > 0u ? static_cast<float>(mSum) / static_cast<float>(valid) : 0.0f;
    const float wAvg = valid > 0u ? (static_cast<float>(weightMilli) / 1000.0f) / static_cast<float>(valid) : 0.0f;
    char line[512];
    std::snprintf(line, sizeof(line),
        "ReSTIR лок %u/%u T%u S%u кип%u солн%u лучи %u диск %.2f Tист %u Tгл %.2f Tн %.2f Tлуч %u Sсос %u Sрад %.0f Sлуч %u кадр %u tlas %u истор %u пов %u жив %u вид %u тень %u M %.1f W %.2f",
        p.powered, localCount, p.knobTemporalOn, p.knobSpatialOn, p.knobBoilOn, p.knobSunOn, p.knobSunRays,
        static_cast<double>(p.knobSunDisk), p.knobHistory, static_cast<double>(p.knobTemporalDepth),
        static_cast<double>(p.knobTemporalNormal), p.knobTemporalBias, p.knobSpatial, static_cast<double>(p.knobRadius),
        p.knobSpatialBias, ctx.GetFrameIndex(),
        tlas != 0u ? 1u : 0u, hist, surf, valid, vis, shadow, static_cast<double>(mAvg), static_cast<double>(wAvg));
    publishLog(p, 1u, localCount, infCount, tlas != 0u ? 1u : 0u, valid > 0u ? 1u : 0u, 0u, line);

    std::memcpy(p.prevViewProj[flight], view.viewProj, sizeof(view.viewProj));
    std::memcpy(p.prevInvViewProj[flight], view.invViewProj, sizeof(view.invViewProj));
    std::memcpy(p.prevCamera[flight], view.cameraPos, sizeof(view.cameraPos));
    p.historyValid[flight] = 1;
    p.parity[flight] ^= 1u;
    ctx.SetFrameIndex(ctx.GetFrameIndex() + 1u);
}

} // namespace burnhope
