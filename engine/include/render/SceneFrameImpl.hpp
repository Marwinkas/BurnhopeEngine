#pragma once

#include "debug/DebugStats.hpp"

#include <cstdint>
#include <vector>
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
#include "render/Sharc.hpp"
#include "render/Fsr.hpp"
#include "render/Gtao.hpp"
#include "render/Restir.hpp"
#include "render/Voxel.hpp"
#include "render/SceneFrame.hpp"
#include "render/Gizmo.hpp"
#include "render/Particles.hpp"
#include "render/Fog.hpp"
#include "render/Rain.hpp"
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

#include <chrono>

namespace burnhope {

struct PhysicsWorld;
struct AudioWorld;

constexpr uint32_t kSceneVerts = 512;
constexpr uint32_t kSceneTris = 256;
constexpr uint32_t kSceneInstances = 48;
constexpr uint32_t kPropCap = 16;
constexpr uint32_t kEditorSpare = 8;

struct SceneFrameImpl {
    bool ready = false;
    uint32_t cullLogTick = 0;
    uint32_t shadowLogTick = 0;
    std::chrono::steady_clock::time_point gpuLogAt{};
    FlyCam fly{};
    SceneActors actors{};
    flecs::entity weatherEnt{};
    GizmoPass gizmo{};
    TestScene world{};
    VisPass vis{};
    ShadePass shade{};
    SkyPass sky{};
    GpuBuffer atmBuf{};
    AtmosphereParams atmosphere{};
    float skyTime = 0.0f;
    RadiancePass radiance{};
    SharcPass sharc{};
    RestirPass restir{};
    FsrPass fsr{};
    GtaoPass gtao{};
    VoxelPass voxel{};
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
    bool sunEnabled = true;
    bool sunCast = true;
    bool sunCsm = false;
    float sunKelvin = 5600.0f;
    float sunIndirect = 1.0f;
    bool showBackface = true;
    bool showScreen = true;
    bool showBehind = true;
    uint32_t debugMode = 0;
    GpuBuffer lightBuf{};
    GpuBuffer cellBuf{};
    GpuBuffer lightIndex{};
    GpuBuffer decalBuf{};
    GpuBuffer probeBuf{};
    GpuBuffer sectorBuf{};
    GpuBuffer shadowPageBuf{};
    int32_t pageCell[3][2]{};
    bool pageReady = false;
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
    LightGpuData handLamp{};
    bool handOn = false;
    float frameMs[128]{};
    uint32_t frameCursor = 0;
    GpuVertex cpuVerts[kSceneVerts]{};
    uint32_t cpuIndex[kSceneTris * 3]{};
    GpuInstance cpuInstances[kSceneInstances]{};
    GpuMeshlet cpuMeshlets[kSceneInstances]{};
    uint32_t vertCount = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t vertCap = 0;
    uint32_t indexCap = 0;
    uint32_t instanceCap = 0;
    uint32_t materialCap = 0;
    uint8_t editorDirty = 0;
    struct PropLink {
        uint32_t instance = 0xffffffffu;
        uint32_t material = 0xffffffffu;
        uint8_t actor = 0xff;
        float radius = 1.0f;
        Xform pose{};
    };
    PropLink props[kPropCap]{};
    PhysicsWorld* physics = nullptr;
    AudioWorld* audio = nullptr;
    ParticlePass particles{};
    FogVolume fog{};
    RainPass rain{};
    float simDt = 0.0f;
    float camShock = 0.0f;
    float camShockVel = 0.0f;
    float camBob = 0.0f;
    float camPrevEye[3]{};
    uint32_t camBursts = 1;
    float impactY[kPropCap]{};
    float impactVy[kPropCap]{};
    char texPath[512]{};
    uint8_t texActor = 0xff;
    uint8_t texChannel = 0xff;
    struct TexJob {
        char path[512]{};
        uint8_t actor = 0xff;
        uint8_t channel = 0xff;
    };
    TexJob texJob[8]{};
    uint8_t texJobN = 0;
    uint8_t texPublish = 0;
    uint32_t texPublishBase = 0;
    uint8_t propCount = 0;
    uint8_t propPhase = 0;
    uint8_t propDirty = 0;
    uint32_t visibleCount = 0;
    const GpuVertex* vertSrc = nullptr;
    const uint32_t* indexSrc = nullptr;
    const GpuInstance* instSrc = nullptr;
    const GpuMeshlet* meshletSrc = nullptr;
    std::vector<GpuMeshlet> meshletGpu;
    std::vector<uint32_t> visibleCpu;
    uint32_t visibleCap = 0;
    MappedFile bhop{};
    float farPlane = 80.0f;
    float nearPlane = 0.1f;
    float fovYRad = 1.0471975512f;
    float savedEye[3]{};
    float savedYaw = 0.0f;
    float savedPitch = 0.0f;
    uint8_t camView = 0;
    uint8_t camSaved = 0;
    uint8_t camOrtho = 0;
    uint8_t camFacing = 0;
    float camZoom = 1.0f;
    uint8_t sim = 0;
    float physicsMs = 0.0f;
    float gpuMs = 0.0f;
    uint32_t sceneTris = 0;
    std::vector<uint64_t> snapId;
    std::vector<uint16_t> snapActor;
    std::vector<uint16_t> snapSize;
    std::vector<uint8_t> snapBytes;
    float prevVp[16]{};
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
    std::vector<GpuImage> textures;
    uint32_t textureSlots = 0;
    const MaterialGpuData* materialSrc = nullptr;
    MaterialGpuData fallbackMat[4]{};
    uint32_t materialCount = 0;
    const SceneTexName* texNames = nullptr;
    uint32_t texNameCount = 0;
    char bhopPath[512]{};
    const Meshlet* cones = nullptr;
    uint32_t coneCount = 0;
    float fps = 0.0f;
    HudPost hud{.gtao = 0.55f};
    GpuProfiler gpuTime{};
    float cpuRecordMs = 0.0f;
    char gpuText[4096]{};
    SceneBlock block{};
    bool freeze = false;
    bool freezeHeld = false;
    float freezeVp[16]{};
    int32_t forceLod = -1;
    uint32_t passMask = 63u;
    float sscsReach = 30.0f;
    float sscsThick = 1.0f;
    float sscsSteps = 20.0f;
    float sscsPx = 128.0f;
    float ssrcBase = 2.0f;
    float ssrcThick = 0.03f;
    float ssrcCap = 0.6f;
    float ssrcSteps = 8.0f;
    float rcRays = 1.0f;
    float rcSpacing = 0.2f;
    float rcIntensity = 1.0f;
    float rcMax = 48.0f;
    float rcSun = 1.0f;
    float rcBounce = 1.0f;
    float giGain = 2.0f;
    bool sharcDenoise = true;
    float bloomThreshold = 0.35f;
    float rainAmount = 3.0f;
    float rainWet = 1.0f;
    float rainRipple = 0.85f;
    uint32_t tonemapper = 0;
    std::vector<GpuVertex> ownedVert;
    std::vector<uint32_t> ownedIndex;
    std::vector<GpuInstance> ownedInst;
    std::vector<GpuMeshlet> ownedMesh;
    std::vector<MaterialGpuData> ownedMat;
    std::vector<SceneTexName> ownedTex;
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
void framePassShade(const RenderContext& ctx, ShadePass& shade, SkyPass& sky, GtaoPass& gtao, TrackedImage& vis, TrackedImage& depth, bool gtaoOn, VkBuffer probes, VkDeviceSize probeBytes, bool restirDi, float fovY);
void framePassRestir(const RenderContext& ctx, RestirPass& restir, const FrameView& view, const void* lights, uint32_t lightCount, uint32_t passMask, VkDeviceAddress tlas);
void framePassShadeLate(const RenderContext& ctx, ShadePass& shade, SkyPass& sky, FogVolume& fog);
void framePassRadiance(const RenderContext& ctx, RadiancePass& rad, TrackedImage& hdrA, bool worldRc, float rays, float spacing, float intensity, float maxDist, float sunScale, float bounce);
void framePassSharc(const RenderContext& ctx, SharcPass& sharc, TrackedImage& hdrA, float giGain, uint32_t lightCount, VkDeviceAddress tlas, const RestirPass& restir, uint32_t restirOn, uint32_t denoise);
void framePassVoxel(const RenderContext& ctx, VoxelPass& voxel, TrackedImage& hdrA, bool worldVx, uint32_t instances, bool moved);
void framePassTonemap(const RenderContext& ctx, TonemapPass& tone, TrackedImage& hdrA, TrackedImage& hdrB, const Swapchain& swap, const FrameContext& frame, bool bloomOn, bool skipAa, bool taaOn);

struct SceneFrame : SceneFrameImpl {};

// Per-frame uniform inputs produced by sceneFillUniforms and consumed by the pass recording in sceneFrameRecord.
struct SceneUniformSet {
    CameraPose pose{};
    FrameView gpu{};
    FrameSun lit{};
    FramePost knob{};
    const LightGpuData* lightSrc = nullptr;
    float unjitVp[16]{};
    float jitInv[16]{};
    float splits[3]{};
    float sunCentre[3]{};
    uint32_t sunKeep[3]{};
    uint32_t flight = 0;
    uint32_t strideU = 0;
    uint32_t lightCount = 0;
    bool taaLive = false;
};

// Scene frame internals shared by SceneFrame.cpp, SceneTextures.cpp, SceneGeometry.cpp, SceneProcedural.cpp,
// SceneUniforms.cpp and SceneTargets.cpp.
void sceneResizeTargets(SceneFrame* scene, Device& device, const Swapchain& swap, const FrameContext& frame, VkExtent2D renderExtent);
bool sceneCreateGpu(SceneFrame* scene, Device& device, const Swapchain& swap);
void sceneFillUniforms(SceneFrame* scene, Device& device, const Swapchain& swap, const FrameContext& frame, bool fsrLive, SceneUniformSet& out);
MaterialGpuData* materialAt(SceneFrameImpl& s, uint32_t id);
void materialOwnFromView(SceneFrameImpl& s, const SceneBlobView& view);
bool nameHas(const char* text, const char* needle);
bool loadTextures(SceneFrameImpl& s, Device& device);
void modelLabel(const SceneFrameImpl& scene, uint32_t mat, char* dst, uint32_t cap);
void syncPropMaterial(SceneFrame& scene);
bool ensureOwnedMaterials(SceneFrameImpl& s);
void takeQueuedTexture(SceneFrameImpl& s, Device& device);
void placeActors(SceneFrameImpl& scene);
void armEditorRoom(SceneFrameImpl& s);
uint8_t linkProp(SceneFrameImpl& s, uint32_t instance, const char* name);
void bakeAudio(SceneFrameImpl& scene);
void uploadStatic(SceneFrameImpl& s);
uint32_t cullLod(SceneFrameImpl& s, const CameraCull& cam);
uint32_t cullPoint(const SceneFrameImpl& s, float lx, float ly, float lz, float radius, uint32_t* out);
uint32_t cullFace(const SceneFrameImpl& s, const Mat4& view, uint32_t* out);
void freeOwned(SceneFrame& s);
bool syncPropInstance(SceneFrame* scene);
bool ensureOwnedInstances(SceneFrameImpl& s);
uint32_t uploadLights(SceneFrameImpl& s, uint32_t flight);
void followHandLamp(SceneFrameImpl& s);
bool heapImage(DescriptorHeaps& heaps, Device& device, SceneFrameImpl& s, uint32_t slot, VkDescriptorType type, const GpuImage& img, VkImageLayout layout, VkImageAspectFlags aspect);
bool bindHeap(SceneFrameImpl& s, Device& device, DescriptorHeaps& heaps);

} // namespace burnhope
