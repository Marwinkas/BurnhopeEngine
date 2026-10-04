#pragma once

#include "debug/DebugStats.hpp"
#include "gpu_scene/Camera.hpp"
#include "gpu_scene/InstanceData.hpp"
#include "gpu_scene/FrameUniforms.hpp"
#include "gpu_scene/MeshletGpu.hpp"
#include "gfx/Decal.hpp"
#include "gfx/Portal.hpp"
#include "gfx/Probe.hpp"
#include "image/ImageFile.hpp"
#include "gfx/Material.hpp"
#include "render/Cull.hpp"
#include "render/PassCommon.hpp"
#include "render/PassTargets.hpp"
#include "render/Cube.hpp"
#include "render/Shade.hpp"
#include "render/Sky.hpp"
#include "render/Hiz.hpp"
#include "render/Radiance.hpp"
#include "render/SceneFrame.hpp"
#include "render/Tonemap.hpp"
#include "render/Visbuffer.hpp"
#include "rhi/Swapchain.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "debug/CpuScope.hpp"
#include "asset/SceneBlob.hpp"
#include "io/File.hpp"
#include "scene/FlyCam.hpp"
#include "scene/TestScene.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

constexpr uint32_t kSceneVerts = 64;
constexpr uint32_t kSceneTris = 64;
constexpr uint32_t kSceneInstances = 4;

struct SceneFrameImpl {
    bool ready = false;
    FlyCam fly{};
    TestScene world{};
    VisPass vis{};
    ShadePass shade{};
    SkyPass sky{};
    GpuBuffer atmBuf{};
    AtmosphereParams atmosphere{};
    float skyTime = 0.0f;
    RadiancePass radiance{};
    CubePass cube{};
    TonemapPass tone{};
    GpuBuffer frameBuf{};
    GpuBuffer sunBuf{};
    GpuBuffer postBuf{};
    DescriptorHeaps* kickHeaps = nullptr;
    DescriptorHeaps gpuHeap{};
    uint32_t kickFlight = 0;
    GpuBuffer verts{};
    GpuBuffer indices{};
    GpuBuffer instances{};
    GpuBuffer meshlets{};
    GpuBuffer visible{};
    GpuBuffer shadowVisible{};
    GpuBuffer pointVisible{};
    GpuBuffer cubeVisible{};
    GpuBuffer candidates{};
    GpuBuffer indirect{};
    OcclPass occl{};
    ClusterPass clusters{};
    ShadowCullPass shadowCull{};
    GpuBuffer shadowIndirect{};
    bool flagFlip[2]{};
    uint32_t lightProfile = 0;
    bool showHiz = true;
    bool showCone = true;
    bool showLod = true;
    bool showFrustum = true;
    bool showSubpixel = true;
    bool showShadow = true;
    uint32_t debugMode = 0;
    GpuBuffer lightBuf{};
    GpuBuffer cellBuf{};
    GpuBuffer lightIndex{};
    GpuBuffer decalBuf{};
    GpuBuffer probeBuf{};
    GpuBuffer sectorBuf{};
    GpuBuffer shadowPageBuf{};
    GpuBuffer texPageBuf{};
    GpuBuffer texFeedbackBuf{};
    DecalGpu decals[kDecalCap]{};
    uint32_t decalCount = 0;
    Sector sectors[32]{};
    Portal portals[64]{};
    uint32_t sectorCount = 0;
    uint32_t portalCount = 0;
    LightGpuData imported[kLightCap]{};
    uint32_t importedCount = 0;
    float frameMs[128]{};
    uint32_t frameCursor = 0;
    GpuVertex cpuVerts[kSceneVerts]{};
    uint32_t cpuIndex[kSceneTris * 3]{};
    GpuInstance cpuInstances[kSceneInstances]{};
    GpuMeshlet cpuMeshlets[kSceneInstances]{};
    uint32_t vertCount = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t visibleCount = 0;
    const GpuVertex* vertSrc = nullptr;
    const uint32_t* indexSrc = nullptr;
    const GpuInstance* instSrc = nullptr;
    const GpuMeshlet* meshletSrc = nullptr;
    GpuMeshlet* meshletGpu = nullptr;
    uint32_t* visibleCpu = nullptr;
    uint32_t visibleCap = 0;
    MappedFile bhop{};
    float farPlane = 80.0f;
    float shadowExtent = 20.0f;
    GpuBuffer materialsBuf{};
    GpuImage shadowColor[2]{};
    TrackedImage shadowDepth[2]{};
    uint32_t shadowSlot = 0;
    float shadowStamp[3][16]{};
    uint32_t shadowStampValid = 0;
    uint32_t shadowStampCount = 0;
    uint32_t shadowCount[3]{};
    uint32_t pointCount[3]{};
    uint32_t cubeCount[6]{};
    TrackedImage pointDepth{};
    bool pointReady = false;
    float pointSig = 0.0f;
    uint32_t pointCursor = 0;
    uint32_t cubeFace = 0;
    float cubeEye[3]{};
    TrackedImage cubeColor{};
    TrackedImage cubeDepth{};
    bool cubeReady = false;
    bool cubeBaked = false;
    HizPass hiz{};
    TrackedImage hizMax[2]{};
    uint32_t occlIn = 0;
    uint32_t occlOut = 0;
    float boxMin[3]{-20.0f, -2.0f, -20.0f};
    float boxMax[3]{20.0f, 20.0f, 20.0f};
    GpuImage* textures = nullptr;
    uint32_t textureSlots = 0;
    const MaterialGpuData* materialSrc = nullptr;
    MaterialGpuData fallbackMat[4]{};
    uint32_t materialCount = 0;
    const SceneTexName* texNames = nullptr;
    uint32_t texNameCount = 0;
    const Meshlet* cones = nullptr;
    uint32_t coneCount = 0;
    float fps = 0.0f;
    HudPost hud{.gtao = 0.55f};
    GpuProfiler gpuTime{};
    float cpuRecordMs = 0.0f;
    char gpuText[1024]{};
    bool freeze = false;
    bool freezeHeld = false;
    float freezeVp[16]{};
    int32_t forceLod = -1;
    uint32_t passMask = 63u;
    float gtaoRadius = 2.5f;
    float gtaoSteps = 6.0f;
    float gtaoSlices = 6.0f;
    float gtaoPower = 1.6f;
    float gtaoFalloff = 1.0f;
    float ssrCutoff = 0.82f;
    float ssrSteps = 32.0f;
    float ssrStride = 0.025f;
    float ssrThick = 0.06f;
    float ssrMax = 180.0f;
    float ssrBias = 0.15f;
    float sscsReach = 30.0f;
    float sscsThick = 1.0f;
    float sscsSteps = 20.0f;
    float sscsPx = 128.0f;
    float ssrcBase = 2.0f;
    float ssrcThick = 0.03f;
    float ssrcCap = 0.6f;
    float ssrcSteps = 8.0f;
    float rcRays = 8.0f;
    float rcSpacing = 0.45f;
    float rcIntensity = 1.0f;
    float rcMax = 48.0f;
    float bloomThreshold = 0.35f;
    float fogDensity = 0.012f;
    float fogHeight = 4.0f;
    float fogScatter = 1.0f;
    uint32_t tonemapper = 0;
    GpuVertex* ownedVert = nullptr;
    uint32_t* ownedIndex = nullptr;
    GpuInstance* ownedInst = nullptr;
    GpuMeshlet* ownedMesh = nullptr;
    MaterialGpuData* ownedMat = nullptr;
    SceneTexName* ownedTex = nullptr;
    VkExtent2D extent{};
    char title[160]{};
};


void framePassClusters(const RenderContext& ctx, ClusterPass& clusters, GpuBuffer& cells, GpuBuffer& lightIndex);
struct SunPassData {
    TrackedImage& depth;
    const GpuBuffer& instances;
    const GpuBuffer& shadowVisible;
    const GpuBuffer& shadowIndirect;
    float (*stamp)[16];
    uint32_t& stampValid;
    uint32_t& stampCount;
    uint32_t strideU = 0;
    const float* sunC = nullptr;
    const float* splits = nullptr;
};

void framePassCull(const RenderContext& ctx, SceneFrameImpl& scene, const CullBuffers& bufs, CullResults& res);
void framePassSun(const RenderContext& ctx, SceneFrameImpl& scene, VisPass& vis, SunPassData& sun);
void framePassPoints(
    const RenderContext& ctx,
    VisPass& vis,
    CubePass& cube,
    TrackedImage& pointDepth,
    TrackedImage& cubeColor,
    TrackedImage& cubeDepth,
    uint32_t lightCount,
    const uint32_t pointCount[3],
    const uint32_t cubeCount[6],
    uint32_t pointCursor,
    bool& pointReady,
    bool& cubeReady,
    uint32_t& cubeFace);
void framePassVis(
    const RenderContext& ctx,
    VisPass& vis,
    OcclPass& occl,
    HizPass& hiz,
    TrackedImage& hizMax,
    const GpuBuffer& indirect,
    const GpuBuffer& candidates,
    const GpuBuffer& visible,
    uint32_t instanceCount,
    bool twoPhase,
    bool showHiz,
    const VisInstanceDraw& draw,
    const VisInstanceDraw& lateDraw);
void framePassShade(const RenderContext& ctx, ShadePass& shade, SkyPass& sky, TrackedImage& vis, TrackedImage& depth, bool gtaoOn);
void framePassRadiance(const RenderContext& ctx, RadiancePass& rad, TrackedImage& hdrA, bool worldRc);
void framePassTonemap(const RenderContext& ctx, TonemapPass& tone, TrackedImage& hdrA, TrackedImage& hdrB, const Swapchain& swap, const FrameContext& frame, bool bloomOn);

} // namespace burnhope
