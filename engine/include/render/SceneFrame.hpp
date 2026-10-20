#pragma once

#include "gfx/Decal.hpp"
#include "gfx/Portal.hpp"
#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Swapchain.hpp"
#include "scene/FlyCam.hpp"

namespace burnhope {

struct SceneFrame;

[[nodiscard]] SceneFrame* sceneFrameCreate();
[[nodiscard]] bool sceneFrameLoadBhop(SceneFrame* scene, const char* path);
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
[[nodiscard]] uint8_t sceneFrameActorDuplicate(SceneFrame* scene);
[[nodiscard]] bool sceneFrameActorDragging(const SceneFrame* scene);
void sceneFrameActorEuler(const SceneFrame* scene, uint8_t index, float& pitch, float& yaw);
void sceneFrameActorSetEuler(SceneFrame* scene, uint8_t index, float pitch, float yaw);
[[nodiscard]] const char* sceneFrameTitle(const SceneFrame* scene);
void sceneFrameStats(const SceneFrame* scene, char* out, size_t cap);
void sceneFrameDebug(SceneFrame* scene, bool hiz, bool cone, bool lod, bool frustum, bool subpixel, bool shadow, uint32_t mode);

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
    float ssrSteps = 128.0f;
    float ssrStride = 1.0f;
    float ssrThick = 0.6f;
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
    float sunZenith = 35.0f;
    float sunAzimuth = 30.0f;
    float cloudCoverage = 0.42f;
    float cloudDensity = 0.08f;
    uint32_t tonemapper = 0;
    bool freeze = false;
};

void sceneFrameTweaks(SceneFrame* scene, const SceneTweaks& tweaks);
[[nodiscard]] const char* sceneFrameGpuText(const SceneFrame* scene);
void sceneFrameShift(SceneFrame* scene, float dx, float dy, float dz);
[[nodiscard]] bool sceneFrameAppendBhop(SceneFrame* scene, const char* path);
// Ползунки панели. Имена, не индексы post[12].
struct HudPost {
    float grain = 0.0f;
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
