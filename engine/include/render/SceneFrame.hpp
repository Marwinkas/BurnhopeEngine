#pragma once

#include "gfx/Decal.hpp"
#include "gfx/Material.hpp"
#include "gfx/Portal.hpp"
#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Swapchain.hpp"
#include "scene/FlyCam.hpp"

namespace burnhope {

struct SceneFrame;

[[nodiscard]] SceneFrame* sceneFrameCreate();
[[nodiscard]] bool sceneFrameLoadBhop(SceneFrame* scene, const char* path, bool plant = true);
void sceneFrameBlank(SceneFrame* scene);
void sceneFramePlantCubes(SceneFrame* scene);
void sceneFrameDestroy(SceneFrame* scene, Device& device);
void sceneFrameReleaseGpu(SceneFrame* scene, Device& device);
[[nodiscard]] bool sceneFrameTick(SceneFrame* scene, const InputFrame& input, float dt);
// Один показанный кадр. dt — секунды между vkQueuePresentKHR, не между тиками хоста.
void sceneFramePresented(SceneFrame* scene, float dt);
[[nodiscard]] uint8_t sceneFrameActorCount(const SceneFrame* scene);
void sceneFrameActorName(const SceneFrame* scene, uint8_t index, char* name, uint8_t cap, uint8_t* kind);
void sceneFrameActorSelect(SceneFrame* scene, uint8_t index);
[[nodiscard]] uint8_t sceneFrameActorSelected(const SceneFrame* scene);
[[nodiscard]] uint8_t sceneFrameActorKind(const SceneFrame* scene, uint8_t index);
void sceneFrameActorGizmo(SceneFrame* scene, uint8_t mode);
void sceneFrameGizmoSpace(SceneFrame* scene, uint8_t worldSpace);
void sceneFrameSnap(SceneFrame* scene, uint8_t channel, uint8_t enabled, float step);
void sceneFrameCamera(SceneFrame* scene, float fovDeg, float nearZ, float farZ);
void sceneFrameCameraSpeed(SceneFrame* scene, float speed);
void sceneFrameCameraView(SceneFrame* scene, uint8_t view);
void sceneFrameCameraProject(SceneFrame* scene, uint8_t ortho);
void sceneFrameCameraRelease(SceneFrame* scene);
[[nodiscard]] uint8_t sceneFrameCameraMode(const SceneFrame* scene);
[[nodiscard]] uint8_t sceneFrameCameraProject(const SceneFrame* scene);
void sceneFrameGizmoPivot(SceneFrame* scene, uint8_t center);
void sceneFramePlay(SceneFrame* scene);
void sceneFrameStop(SceneFrame* scene);
void sceneFramePause(SceneFrame* scene, uint8_t paused);
[[nodiscard]] uint8_t sceneFrameSim(const SceneFrame* scene);
[[nodiscard]] float sceneFrameFps(const SceneFrame* scene);
[[nodiscard]] float sceneFramePhysicsMs(const SceneFrame* scene);
[[nodiscard]] float sceneFrameGpuMs(const SceneFrame* scene);
[[nodiscard]] uint32_t sceneFrameTriangles(const SceneFrame* scene);
[[nodiscard]] uint32_t sceneFrameDraws(const SceneFrame* scene);
[[nodiscard]] bool sceneFrameActorPivot(const SceneFrame* scene, uint8_t index, float out[3]);
void sceneFrameActorSetPivot(SceneFrame* scene, uint8_t index, const float local[3]);
[[nodiscard]] uint8_t sceneFrameSpawn(SceneFrame* scene, uint8_t kind);
[[nodiscard]] bool sceneFrameCloneMaterial(SceneFrame* scene, uint8_t actor);
void sceneFrameQueueTexture(SceneFrame* scene, uint8_t actor, uint8_t channel, const char* path);
[[nodiscard]] bool sceneFrameApplyDoc(SceneFrame* scene, uint8_t actor, const BhmatDoc& doc);
[[nodiscard]] bool sceneFrameReadDoc(const SceneFrame* scene, uint8_t actor, BhmatDoc& doc);
[[nodiscard]] uint8_t sceneFramePick(const SceneFrame* scene, float x, float y);
void sceneFrameMark(SceneFrame* scene, uint8_t actor);
[[nodiscard]] float sceneFrameCameraFov(const SceneFrame* scene);
[[nodiscard]] float sceneFrameCameraNear(const SceneFrame* scene);
[[nodiscard]] float sceneFrameCameraFar(const SceneFrame* scene);
[[nodiscard]] float sceneFrameCameraSpeed(const SceneFrame* scene);
[[nodiscard]] uint8_t sceneFrameActorDuplicate(SceneFrame* scene);
[[nodiscard]] bool sceneFrameActorDragging(const SceneFrame* scene);
void sceneFrameActorEuler(const SceneFrame* scene, uint8_t index, float& pitch, float& yaw);
void sceneFrameActorSetEuler(SceneFrame* scene, uint8_t index, float pitch, float yaw);

constexpr uint8_t kSecTransform = 1u;
constexpr uint8_t kSecMaterial = 2u;
constexpr uint8_t kSecPost = 4u;
constexpr uint8_t kSecAtmo = 8u;
constexpr uint8_t kSecSun = 16u;
constexpr uint8_t kSecWeather = 32u;
constexpr uint8_t kSecMatFile = 64u;

struct ActorPose {
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    uint8_t isStatic = 0;
};

struct ActorPbr {
    float base[3]{1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissive = 0.0f;
};

[[nodiscard]] uint8_t sceneFrameActorSections(const SceneFrame* scene, uint8_t index);
[[nodiscard]] bool sceneFrameActorPose(const SceneFrame* scene, uint8_t index, ActorPose& out);
void sceneFrameActorSetPose(SceneFrame* scene, uint8_t index, const ActorPose& pose);
[[nodiscard]] bool sceneFrameActorPbr(const SceneFrame* scene, uint8_t index, ActorPbr& out);
void sceneFrameActorSetPbr(SceneFrame* scene, uint8_t index, const ActorPbr& pbr);
[[nodiscard]] uint8_t sceneFrameSpawnCube(SceneFrame* scene);
[[nodiscard]] const char* sceneFrameTitle(const SceneFrame* scene);
void sceneFrameStats(const SceneFrame* scene, char* out, size_t cap);
void sceneFrameDebug(SceneFrame* scene, bool hiz, bool cone, bool lod, bool frustum, bool subpixel, bool shadow, bool backface, bool screen, bool behind, uint32_t mode);

struct SceneTweaks {
    int32_t forceLod = -1;
    // Биты как у галок по умолчанию. 128 (RC) и 64 (заморозка) выключены: иначе первый кадр,
    // ещё до чтения холста, печёт BLAS и теряет устройство.
    uint32_t passMask = 63u;
    float gtaoRadius = 0.5f;
    float gtaoSteps = 3.0f;
    float gtaoSlices = 3.0f;
    float gtaoPower = 2.2f;
    float gtaoFalloff = 0.615f;
    float gtaoDenoise = 1.0f;
    float ssrCutoff = 0.82f;
    float ssrSteps = 64.0f;
    float ssrStride = 1.0f;
    float ssrThick = 0.02f;
    float ssrMetal = 0.35f;
    float ssrRt = 0.45f;
    float ssrEdge = 24.0f;
    float ssrMax = 80.0f;
    float ssrBias = 0.15f;
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
    float restirLocal = 8.0f;
    float restirSpatial = 3.0f;
    float restirRadius = 4.0f;
    float restirHistory = 20.0f;
    float restirBoil = 0.2f;
    float restirTemporalDepth = 0.1f;
    float restirTemporalNormal = 0.9f;
    float restirTemporalBias = 1.0f;
    float restirSpatialDepth = 0.1f;
    float restirSpatialNormal = 0.9f;
    float restirSpatialBias = 3.0f;
    float restirSpatialBoost = 3.0f;
    float restirSunDisk = 0.5f;
    float restirSunRays = 1.0f;
    float restirShadowRays = 1.0f;
    float restirShadowRange = 0.0f;
    float restirShadowBias = 0.05f;
    float restirPermTh = 0.9f;
    float restirTarget = 20.0f;
    bool restirTemporal = true;
    bool restirSpatialOn = true;
    bool restirBoilOn = true;
    bool restirInitVis = false;
    bool restirPerm = true;
    bool restirShortcut = false;
    bool restirMaterial = true;
    bool restirNaive = true;
    bool restirSunOn = true;
    uint32_t fsrQuality = 0;
    float fsrSharpness = 0.2f;
    uint32_t lightProfile = 0;
    float bloomThreshold = 0.35f;
    float fogDensity = 0.06f;
    float fogHeight = 0.0f;
    float fogScatter = 40.0f;
    float fogFalloff = 0.08f;
    float fogR = 0.62f;
    float fogG = 0.72f;
    float fogB = 0.86f;
    float fogLum = 1.4f;
    float fogStart = 0.0f;
    float fogOpacity = 0.92f;
    float fogInStart = 0.0f;
    float rainAmount = 3.0f;
    float rainWet = 1.0f;
    float rainRipple = 0.85f;
    float sunZenith = 35.0f;
    float sunAzimuth = 30.0f;
    float cloudCoverage = 0.42f;
    float cloudDensity = 0.08f;
    float windSpeed = 6.0f;
    float windYaw = 23.0f;
    float temperatureC = 12.0f;
    float dayLength = 1200.0f;
    float timeOfDay = 11.4f;
    int clockSun = 0;
    float sunKelvin = 5600.0f;
    float sunAngle = 0.53f;
    float sunSoft = 0.5f;
    float sunIndirect = 1.0f;
    int sunEnabled = 1;
    int sunCast = 1;
    int sunCsm = 0;
    uint32_t tonemapper = 0;
    bool freeze = false;
};

void sceneFrameTweaks(SceneFrame* scene, const SceneTweaks& tweaks);
void sceneFrameWeatherPreset(SceneFrame* scene, uint32_t preset);
[[nodiscard]] float sceneFrameWeatherTime(const SceneFrame* scene);
[[nodiscard]] float sceneFrameWeatherZenith(const SceneFrame* scene);
[[nodiscard]] float sceneFrameWeatherAzimuth(const SceneFrame* scene);
[[nodiscard]] const char* sceneFrameGpuText(const SceneFrame* scene);
void sceneFrameShift(SceneFrame* scene, float dx, float dy, float dz);
[[nodiscard]] bool sceneFrameAppendBhop(SceneFrame* scene, const char* path);
// Ползунки панели. Имена, не индексы post[12].
struct HudPost {
    float grain = 0.04f;
    float brightness = 1.0f;
    float bloom = 0.15f;
    float vignette = 1.1f;
    float ca = 2.0f;
    float contact = 1.0f;
    float specOcc = 1.0f;
    float toksvig = 1.0f;
    float pom = 0.35f;
    float ggx = 1.0f;
    float ssrc = 1.0f;
    float gtao = 1.2f;
    float lamp = 1.0f;
    float sunGain = 1.0f;
    float sunR = 1.0f;
    float sunG = 0.92f;
    float sunB = 0.76f;
    float exposure = -1.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float night = 0.0f;
    float shadows = 1.0f;
    float midtones = 1.0f;
    float highlights = 1.0f;
    float slope = 1.0f;
    float cdlOffset = 0.0f;
    float power = 1.0f;
    float dofFocus = 8.0f;
    float dofAmount = 0.0f;
    float flare = 0.0f;
    float gradeFlags = 0.0f;
    float mix = 0.0f;
    float genContrast = 1.0f;
    float midIn = 0.18f;
    float midOut = 0.18f;
    float hdrMax = 10.0f;
    float vibrance = 1.0f;
};

void sceneFrameTune(SceneFrame* scene, const HudPost& post);
void sceneFrameSetDecals(SceneFrame* scene, const DecalGpu* decals, uint32_t count);
void sceneFrameSetSectors(SceneFrame* scene, const Sector* sectors, uint32_t sectorCount, const Portal* portals, uint32_t portalCount);
// Сабмит сборки BLAS, если она уже записана. Звать после present, не из записи кадра.
void sceneFrameKick(SceneFrame* scene, Device& device);
void sceneFrameRecord(
    SceneFrame* scene,
    VkCommandBuffer cmd,
    Device& device,
    DescriptorHeaps& heaps,
    const Swapchain& swap,
    const FrameContext& frame);

} // namespace burnhope
