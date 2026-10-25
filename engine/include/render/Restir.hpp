#pragma once

#include "gfx/Light.hpp"
#include "gpu_scene/FrameUniforms.hpp"
#include "render/PassCommon.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

#include <Rtxdi/DI/ReSTIRDIParameters.h>

#include <cstdint>
#include <vector>

namespace rtxdi {
class ReSTIRDIContext;
}

namespace burnhope {

constexpr uint32_t kPassRestir = 1u << 20;
constexpr uint32_t kRestirTileSize = 1024;
constexpr uint32_t kRestirTileCount = 128;
constexpr uint32_t kRestirNeighborCount = 8192;
constexpr uint32_t kRestirConstStride = 512;

struct alignas(16) RestirSlot {
    uint32_t lightData = 0;
    uint32_t uvData = 0;
    uint32_t mVisibility = 0;
    uint32_t distanceAge = 0;
    float targetPdf = 0.0f;
    float weight = 0.0f;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};
static_assert(sizeof(RestirSlot) == 32);
static_assert(alignof(RestirSlot) == 16);

struct RestirPush {
    uint32_t pass = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};
static_assert(sizeof(RestirPush) == 16);

struct RestirConstants {
    RTXDI_RuntimeParameters runtime{};
    RTXDI_ReservoirBufferParameters reservoir{};
    RTXDI_DIBufferIndices indices{};
    RTXDI_DIInitialSamplingParameters initial{};
    RTXDI_DITemporalResamplingParameters temporal{};
    RTXDI_DISpatialResamplingParameters spatial{};
    RTXDI_LightBufferParameters lights{};
    RTXDI_RISBufferSegmentParameters localRis{};
    RTXDI_RISBufferSegmentParameters envRis{};
    RTXDI_BoilingFilterParameters boiling{};
    float prevViewProj[16]{};
    float prevInvViewProj[16]{};
    float prevCamera[4]{};
    uint32_t historyValid = 0;
    uint32_t readSlice = 0;
    uint32_t writeSlice = 0;
    uint32_t pdfWidth = 0;
    uint32_t pdfHeight = 0;
    uint32_t tlasLo = 0;
    uint32_t tlasHi = 0;
    uint32_t reservoirBase = 0;
    uint32_t lightCount = 0;
    float sunDisk = 0.5f;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
    float shadowRange = 0.0f;
    float shadowBias = 0.05f;
    uint32_t shadowRays = 1;
    uint32_t pad3 = 0;
};
static_assert(sizeof(RTXDI_RuntimeParameters) == 16);
static_assert(sizeof(RTXDI_ReservoirBufferParameters) == 16);
static_assert(sizeof(RTXDI_DIBufferIndices) == 32);
static_assert(sizeof(RTXDI_DIInitialSamplingParameters) == 48);
static_assert(sizeof(RTXDI_DITemporalResamplingParameters) == 32);
static_assert(sizeof(RTXDI_DISpatialResamplingParameters) == 48);
static_assert(sizeof(RTXDI_LightBufferParameters) == 48);
static_assert(sizeof(RTXDI_RISBufferSegmentParameters) == 16);
static_assert(sizeof(RTXDI_BoilingFilterParameters) == 16);
static_assert(offsetof(RestirConstants, prevViewProj) == 288);
static_assert(offsetof(RestirConstants, prevCamera) == 416);
static_assert(offsetof(RestirConstants, historyValid) == 432);
static_assert(sizeof(RestirConstants) == 496);

struct DescriptorHeaps;

struct RestirPass {
    ShaderExt presample{};
    ShaderExt di{};
    ShaderExt fast{};
    ShaderExt sigma{};
    GpuImage sigmaPenumbra{};
    GpuImage sigmaSun{};
    GpuImage sigmaHist{};
    VkImageLayout sigmaPenumbraLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout sigmaSunLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout sigmaHistLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t sigmaPing[2]{};
    GpuBuffer reservoirs{};
    GpuBuffer neighbors{};
    GpuBuffer ris{};
    GpuBuffer constants{};
    GpuBuffer pdfStaging{};
    GpuImage pdf[2]{};
    GpuImage histPos{};
    GpuImage histMat{};
    std::vector<float> pdfCpu{};
    rtxdi::ReSTIRDIContext* ctx[2]{};
    uint32_t pdfW = 0;
    uint32_t pdfH = 0;
    uint32_t pdfMips = 0;
    uint32_t pdfFloats = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VkImageLayout pdfLayout[2]{VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout histPosLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout histMatLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    float prevViewProj[2][16]{};
    float prevInvViewProj[2][16]{};
    float prevCamera[2][4]{};
    GpuBuffer statRead{};
    uint32_t parity[2]{};
    uint32_t sunSlice[2]{};
    uint32_t historyValid[2]{};
    uint32_t statReady[2]{};
    uint32_t knobLocal = 8;
    uint32_t knobSpatial = 3;
    uint32_t knobHistory = 20;
    uint32_t knobSpatialBoost = 3;
    uint32_t knobTemporalBias = 1;
    uint32_t knobSpatialBias = 3;
    uint32_t knobTemporalOn = 1;
    uint32_t knobSpatialOn = 1;
    uint32_t knobBoilOn = 1;
    uint32_t knobInitVis = 0;
    uint32_t knobPerm = 1;
    uint32_t knobShortcut = 0;
    uint32_t knobMaterial = 1;
    uint32_t knobNaive = 1;
    uint32_t knobSunOn = 1;
    uint32_t knobSunRays = 1;
    uint32_t knobShadowRays = 1;
    float knobShadowRange = 0.0f;
    float knobShadowBias = 0.05f;
    uint32_t knobTarget = 20;
    float knobRadius = 4.0f;
    float knobBoil = 0.2f;
    float knobTemporalDepth = 0.1f;
    float knobTemporalNormal = 0.9f;
    float knobSpatialDepth = 0.1f;
    float knobSpatialNormal = 0.9f;
    float knobSunDisk = 0.5f;
    float knobPermTh = 0.9f;
    uint32_t powered = 0;
    uint64_t logMs = 0;
    uint32_t logActive = 0xffffffffu;
    uint32_t logLights = 0xffffffffu;
    uint32_t logPowered = 0xffffffffu;
    uint32_t logTlas = 0xffffffffu;
    uint32_t logAlive = 0xffffffffu;
    uint32_t logSkip = 0xffffffffu;
    float powerSum = 0.0f;
    char lastLog[512]{};
    bool cleared = false;
    bool statOpen = false;
    uint32_t shownDi = 0;
    uint32_t shownSun = 0;
    uint32_t shownGi = 0;
    uint32_t shownRefl = 0;
};

struct GpuProfiler;

[[nodiscard]] bool restirCreate(RestirPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void restirDestroy(RestirPass& p, Device& d);
[[nodiscard]] bool restirResize(RestirPass& p, Device& d, VkExtent2D extent);
[[nodiscard]] bool restirBind(RestirPass& p, Device& d, DescriptorHeaps& heaps);
void restirNote(RestirPass& p, uint32_t profile, const char* why);
void restirStatReset(VkCommandBuffer cmd, RestirPass& p, Device& d, uint32_t flight);
void restirStatFlush(VkCommandBuffer cmd, RestirPass& p, uint32_t flight);
void restirRecord(
    VkCommandBuffer cmd,
    RestirPass& p,
    Device& d,
    const FrameView& view,
    const void* lights,
    uint32_t lightCount,
    uint32_t passMask,
    uint32_t flight,
    VkExtent2D extent,
    VkDeviceAddress tlas,
    GpuProfiler& profiler);

} // namespace burnhope
